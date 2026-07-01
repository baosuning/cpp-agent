#include "plan_execute_loop.h"
#include "context_manager.h"
#include <agent/i_tool.h>
#include <agent/i_mcp.h>
#include <agent/i_memory.h>
#include <agent/personality.h>
#include <agent/i_context_manager.h>
#include "../util/utf8_utils.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <cstring>
#include <future>
#include <functional>

// 统一日志宏（底层使用 util/log.h 的统一日志系统）
#define PE_DEBUG(...) AGENT_LOG_DEBUG_IF(config_.base.debug, "PlanExec") << __VA_ARGS__
#define PE_INFO(...)  AGENT_LOG_INFO("PlanExec") << __VA_ARGS__
#define PE_ERROR(...) AGENT_LOG_ERROR("PlanExec") << __VA_ARGS__

#ifdef _WIN32
#include <windows.h>
#endif

namespace agent {

PlanExecuteLoop::PlanExecuteLoop(LlmProviderPtr llm_provider,
                                 UserConfirmHandlerPtr confirm_handler,
                                 IContextManager& context,
                                 IPromptBuilder& prompt_builder,
                                 IToolRegistry& tools,
                                 IMcpManager& mcps,
                                 IMemory& memory,
                                 const PersonalityDocs& personality,
                                 PlanExecuteLoopConfig config,
                                 LlmProviderPtr planner_llm,
                                 LlmProviderPtr executor_llm,
                                 TokenUsageAccumulator* token_accumulator)
    : AgentLoopBase(llm_provider,
                    std::move(confirm_handler),
                    context, prompt_builder, tools, mcps, memory,
                    personality, static_cast<InnerLoopConfig>(config), token_accumulator)
    , current_plan_{}
    , current_step_index_{-1}
    , paused_for_user_input_{false}
    , planner_llm_(planner_llm ? std::move(planner_llm) : llm_provider)
    , executor_llm_(executor_llm ? std::move(executor_llm) : llm_provider)
    , config_(std::move(config))
{}

PlanExecuteLoop::~PlanExecuteLoop() = default;

void PlanExecuteLoop::run(const u8str& user_input) {
    try {
        set_state(AgentState::Thinking);

        // 基类通用状态重置（未来新增的基类重置逻辑会自动生效）
        reset_loop_state();

        // Plan-and-Execute 特有状态重置
        current_plan_ = Plan{};
        current_step_index_ = -1;
        paused_for_user_input_ = false;
        execution_log_.reset();
        plan_graph_.reset();
        replan_count_ = 0;

        // 预计算 tools_schema（在整个 run() 中不变）
        try {
            cached_tools_schema_ = build_combined_tools_schema();
        } catch (const std::exception&) {
            // 构建失败时 schema 为空，后续代码会容错处理
            cached_tools_schema_ = nlohmann::json::array();
        }

        context_.add_user_message(prompt_builder_.build_user_prompt(user_input, personality_docs_));

        auto relevant_memories = memory_.search(user_input);
        if (!relevant_memories.empty()) {
            u8str memory_context;
            for (const auto& mem : relevant_memories) {
                memory_context += mem + u8str(u8"\n");
            }
            context_.add_system_message(memory_context);
        }

        auto personality_docs = personality_docs_;
        u8str system_prompt = prompt_builder_.build_system_prompt(personality_docs, plan_execute_instruction());

        u8str plan_text;  // 在 Phase 1 外声明，供 Phase 2 使用

        // ===== Phase 1: Planning =====
        {
            LlmRequest plan_request;
            plan_request.messages = context_.get_messages();
            plan_request.system_prompt = system_prompt;

            // 规划阶段传递工具列表，让 LLM 知道有哪些工具可用（用于制定 tool_hint）
            // 但如果 LLM 返回了 tool_calls，直接忽略——规划阶段只输出计划文本
            plan_request.tools = cached_tools_schema_;
            PE_DEBUG("Phase 1: Planning with " << plan_request.tools.size() << " tools available (for reference only)");

            LlmResponse plan_response = call_llm_for_plan(plan_request);
            if (plan_response.is_error) {
                PE_ERROR("LLM error during planning: " << std::string(plan_response.error_message.begin(), plan_response.error_message.end()));
                final_output_ = u8str(u8"[LLM Error] ") + plan_response.error_message;
                set_state(AgentState::Error);
                return;
            }

            PE_DEBUG("Plan response received, content length: " << plan_response.content.size()
                      << ", tool_calls: " << plan_response.tool_calls.size());

            // 规划阶段忽略 LLM 返回的 tool_calls，只使用文本内容解析计划
            // LLM 可能"违规"在规划阶段调用工具，但我们不执行，只取计划文本
            if (!plan_response.tool_calls.empty()) {
                PE_DEBUG("LLM returned " << plan_response.tool_calls.size()
                          << " tool_calls in planning phase, ignoring (plan-only phase)");
                plan_response.tool_calls.clear();
            }

            if (config_.base.enable_thinking) {
                ThinkingStep thinking_step;
                thinking_step.thinking_content = plan_response.content;
                thinking_step.timestamp = std::chrono::system_clock::now();
                add_thinking_step(std::move(thinking_step));
            }

            plan_text = plan_response.content;
            current_plan_ = parse_plan(plan_text);

            // 暂不将 plan_text 加入 context——等确认计划有效后再加
            // 如果计划为空需要重试，则不应把无效的 plan_text 留在 context 中

            PE_DEBUG("Plan has " << current_plan_.steps.size() << " step(s)");
            for (size_t si = 0; si < current_plan_.steps.size(); ++si) {
                std::string desc(current_plan_.steps[si].description.begin(), current_plan_.steps[si].description.end());
                PE_INFO("  Step " << (si + 1) << ": " << desc);
            }

            // 如果 LLM 没有输出计划格式，重试一次（提示 LLM 必须输出 PLAN 格式）
            if (current_plan_.steps.empty()) {
                PE_DEBUG("Plan is empty, requesting LLM to output plan in required format");

                u8str retry_msg = u8str(u8"Your response did not contain a plan in the required format. "
                    "You MUST output your plan using PLAN: and PLAN_END markers. "
                    "For example:\n"
                    "PLAN:\n"
                    "[{\"id\": \"1\", \"description\": \"...\", \"tool_hint\": \"...\"}]\n"
                    "PLAN_END\n"
                    "Please create a plan now. Do NOT call any tools.");

                context_.add_user_message(retry_msg);

                LlmRequest retry_request;
                retry_request.messages = context_.get_messages();
                retry_request.system_prompt = system_prompt;
                // 重试时也传工具列表（供 LLM 参考），但忽略 tool_calls
                retry_request.tools = cached_tools_schema_;
                LlmResponse retry_response = call_llm_for_plan(retry_request);
                if (retry_response.is_error) {
                    PE_ERROR("LLM error during plan retry: " << std::string(retry_response.error_message.begin(), retry_response.error_message.end()));
                    // 降级：将第一次的响应作为直接回答
                    set_final_output(plan_text);
                    memory_.store(u8str(u8"last_interaction"), plan_text);
                    set_state(AgentState::Completed);
                    return;
                }

                // 忽略重试响应中的 tool_calls
                if (!retry_response.tool_calls.empty()) {
                    PE_DEBUG("Plan retry: LLM still returned tool_calls, ignoring");
                    retry_response.tool_calls.clear();
                }

                plan_text = retry_response.content;
                // 暂不加入 context——等确认计划解析成功后再加

                if (config_.base.enable_thinking) {
                    ThinkingStep thinking_step;
                    thinking_step.thinking_content = retry_response.content;
                    thinking_step.phase = u8str(u8"planning_retry");
                    thinking_step.timestamp = std::chrono::system_clock::now();
                    add_thinking_step(std::move(thinking_step));
                }

                try {
                    current_plan_ = parse_plan(plan_text);
                } catch (const std::exception& e) {
                    PE_ERROR("Plan retry parse failed: " << e.what());
                }
            }

            if (current_plan_.steps.empty()) {
                // 两次尝试都没有得到计划，将响应作为直接回答
                PE_DEBUG("No plan produced after retry, treating as direct answer");
                set_final_output(plan_text);
                memory_.store(u8str(u8"last_interaction"), plan_text);
                set_state(AgentState::Completed);
                return;
            }

            // 计划有效，加入上下文（无论是否经过重试，plan_text 此时都是最终版本）
            context_.add_assistant_message(plan_text);

            // 基于最终计划创建依赖图（必须在 retry 之后，此时 current_plan_ 已确定）
            plan_graph_ = std::make_unique<PlanGraph>(current_plan_);
            if (plan_graph_->has_cycle()) {
                PE_ERROR("Plan has circular dependencies, falling back to linear execution");
                plan_graph_.reset();
            }

            {
                std::string steps_str = std::to_string(current_plan_.steps.size());
                u8str plan_summary = u8str(u8"Plan created with ") + u8str_util::to_u8str(steps_str) + u8str(u8" steps");
                std::lock_guard<std::mutex> lock(output_mutex_);
                final_output_ = plan_summary;
            }
        }

        // Phase 1 完成，检查是否有 tool_calls 需要处理
        // （在 Phase 1 中已经解析了 plan，但 LLM 可能同时返回了 plan 文本和 tool_calls）
        // 注意：此时 plan_response.tool_calls 可能已经在 Phase 1 中被处理过了
        // 如果还有未处理的 tool_calls（理论上不会发生，因为 Phase 1 会处理完），这里不再重复处理

        // 初始化执行日志（Phase 1 结束后，plan_text 已确定，包括重试后的最终版本）
        execution_log_.emplace();
        execution_log_->plan_start_time = std::chrono::system_clock::now();
        execution_log_->original_plan = plan_text;

        // 创建执行指令（供 Phase 2 使用）
        u8str exec_instruction = execution_instruction();

        // ===== Phase 2: Execute each step =====
        PE_DEBUG("Phase 2: Starting execution of " << current_plan_.steps.size() << " steps");

        u8str exec_system_prompt = prompt_builder_.build_system_prompt(
            personality_docs_,
            exec_instruction
        );

        paused_for_user_input_ = false;

        if (plan_graph_) {
            // ===== 基于依赖图的执行 =====
            int steps_executed = 0;
            while (plan_graph_->has_remaining_steps() && steps_executed < config_.base.max_steps) {
                if (stopped_.load()) {
                    set_state(AgentState::Idle);
                    return;
                }

                auto ready_steps = plan_graph_->get_ready_steps();
                if (ready_steps.empty()) {
                    // 没有可执行步骤但还有剩余 → 所有剩余步骤的依赖都失败了
                    PE_DEBUG("No ready steps but remaining steps exist - dependencies unmet");
                    break;
                }

                // 执行第一个就绪步骤（暂不并行）
                auto& step = ready_steps[0];
                current_step_index_ = steps_executed;

                // 条件检查
                if (!should_execute_step(step)) {
                    PE_DEBUG("Step " << std::string(step.id.begin(), step.id.end())
                              << " skipped (condition not met)");
                    // 更新 current_plan_ 中的步骤状态
                    for (auto& s : current_plan_.steps) {
                        if (s.id == step.id) {
                            s.status = u8str(u8"skipped");
                            break;
                        }
                    }
                    plan_graph_->mark_skipped(step.id);
                    continue;
                }

                // 更新 current_plan_ 中的步骤状态为 in_progress
                for (auto& s : current_plan_.steps) {
                    if (s.id == step.id) {
                        s.status = u8str(u8"in_progress");
                        s.start_time = std::chrono::system_clock::now();
                        break;
                    }
                }

                set_state(AgentState::Thinking);

                std::string step_desc(step.description.begin(), step.description.end());
                PE_DEBUG("Phase 2: Executing step " << std::string(step.id.begin(), step.id.end())
                          << ": " << step_desc);

                // 构建步骤提示并执行
                u8str step_prompt = build_step_prompt(step, steps_executed, static_cast<int>(current_plan_.steps.size()));
                context_.add_user_message(step_prompt);
                execute_single_step(step_prompt, exec_system_prompt);

                if (stopped_.load()) {
                    set_state(AgentState::Idle);
                    return;
                }

                // 更新 current_plan_ 中的步骤结果和时间
                {
                    std::lock_guard<std::mutex> lock(output_mutex_);
                    if (final_output_.has_value()) {
                        for (auto& s : current_plan_.steps) {
                            if (s.id == step.id) {
                                s.result = final_output_.value();
                                s.end_time = std::chrono::system_clock::now();
                                break;
                            }
                        }
                    }
                }

                // 统一处理步骤结果
                {
                    // 在 current_plan_ 中找到对应步骤
                    PlanStep* cur_step = nullptr;
                    for (auto& s : current_plan_.steps) {
                        if (s.id == step.id) { cur_step = &s; break; }
                    }
                    if (cur_step) {
                        StepAction action = process_step_result(*cur_step, steps_executed,
                            static_cast<int>(current_plan_.steps.size()));
                        if (action == StepAction::Pause) {
                            paused_for_user_input_ = true;
                            set_state(AgentState::WaitingUserConfirm);
                            break;
                        }
                        if (action == StepAction::Retry) continue;
                        if (action == StepAction::ReplanFromStart) continue;
                        if (action == StepAction::Break) break;
                    }
                }

                if (needs_user_input()) {
                    PE_DEBUG("Step needs user input, pausing execution");
                    paused_for_user_input_ = true;
                    set_state(AgentState::WaitingUserConfirm);
                    break;
                }

                ++steps_executed;
            }
        } else {
        // ===== 降级模式：线性执行 =====
        int steps_executed = 0;
        int i = 0;
        while (i < static_cast<int>(current_plan_.steps.size()) && steps_executed < config_.base.max_steps) {
            if (stopped_.load()) {
                set_state(AgentState::Idle);
                return;
            }

            if (interrupted_.load()) {
                interrupted_ = false;
                u8str new_input;
                {
                    std::lock_guard<std::mutex> lock(input_mutex_);
                    if (!pending_inputs_.empty()) {
                        new_input = std::move(pending_inputs_.front());
                        pending_inputs_.pop();
                    }
                }
                if (!new_input.empty()) {
                    context_.add_user_message(new_input);
                    u8str continue_msg = u8str(u8"User provided additional input, check current plan step: ") + current_plan_.steps[i].description;
                    context_.add_assistant_message(continue_msg);
                }
            }

            // 跳过已完成或已跳过的步骤
            if (current_plan_.steps[i].status == u8str(u8"completed") ||
                current_plan_.steps[i].status == u8str(u8"skipped")) {
                ++i;
                continue;
            }

            // 跳过已失败的步骤（重规划后可能仍有失败步骤保留）
            if (current_plan_.steps[i].status == u8str(u8"failed")) {
                ++i;
                continue;
            }

            auto& step = current_plan_.steps[i];
            current_step_index_ = i;
            step.status = u8str(u8"in_progress");
            step.start_time = std::chrono::system_clock::now();

            set_state(AgentState::Thinking);

            std::string step_desc(step.description.begin(), step.description.end());
            PE_DEBUG("Phase 2: Executing step " << (i + 1) << "/" << current_plan_.steps.size()
                      << ": " << step_desc);

            // 构建步骤提示并执行
            u8str step_prompt = build_step_prompt(step, i, static_cast<int>(current_plan_.steps.size()));
            context_.add_user_message(step_prompt);
            execute_single_step(step_prompt, exec_system_prompt);

            if (stopped_.load()) {
                set_state(AgentState::Idle);
                return;
            }

            // 统一处理步骤结果
            StepAction action = process_step_result(step, i, static_cast<int>(current_plan_.steps.size()));
            if (action == StepAction::Pause) {
                paused_for_user_input_ = true;
                set_state(AgentState::WaitingUserConfirm);
                break;
            }
            if (action == StepAction::Retry) continue;
            if (action == StepAction::ReplanFromStart) {
                i = 0;  // 从头开始执行新计划
                continue;
            }
            if (action == StepAction::Break) break;

            if (needs_user_input()) {
                PE_DEBUG("Step " << (i + 1) << " needs user input, pausing execution");
                paused_for_user_input_ = true;
                set_state(AgentState::WaitingUserConfirm);
                break;
            }

            ++steps_executed;
            ++i;
        }
        } // end of else (linear fallback)

        // ===== Phase 3: Summarize all step results =====
        if (!paused_for_user_input_) {
            int completed = 0, failed = 0;
            for (const auto& step : current_plan_.steps) {
                if (step.status == u8str(u8"completed")) ++completed;
                else if (step.status == u8str(u8"failed")) ++failed;
            }

            u8str final_answer;
            if (completed > 0) {
                // 收集当前 final_output 作为汇总的起点（最后一步的结果）
                {
                    std::lock_guard<std::mutex> lock(output_mutex_);
                    if (final_output_.has_value() && !final_output_->empty()) {
                        final_answer = final_output_.value();
                    }
                }
                // Phase 3: 调用 LLM 综合所有步骤结果生成最终答案
                summarize_all_steps(final_answer);
            }

            // 追加执行摘要尾部
            std::string completed_str = std::to_string(completed);
            std::string total_str = std::to_string(static_cast<int>(current_plan_.steps.size()));
            u8str footer = u8str(u8"\n\n---\n*Execution complete. Steps: ")
                + u8str_util::to_u8str(completed_str)
                + u8str(u8"/") + u8str_util::to_u8str(total_str)
                + u8str(u8" completed*");
            if (failed > 0) {
                footer += u8str(u8", ") + u8str_util::to_u8str(std::to_string(failed)) + u8str(u8" failed.");
            }

            {
                std::lock_guard<std::mutex> lock(output_mutex_);
                final_output_ = final_answer + footer;
            }

            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (on_output_ready_) {
                    on_output_ready_(final_output_.value_or(u8str()));
                }
            }
        }

        memory_.store(u8str(u8"last_interaction"), final_output_.value_or(u8str()));

        // 完成执行日志
        if (execution_log_) {
            execution_log_->plan_end_time = std::chrono::system_clock::now();
            auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                execution_log_->plan_end_time - execution_log_->plan_start_time);
            execution_log_->total_duration_ms = static_cast<int>(total_duration.count());
            execution_log_->replan_count = replan_count_;

            // 计算最终状态
            int completed = 0, failed = 0, total = 0;
            if (plan_graph_) {
                completed = plan_graph_->completed_count();
                failed = plan_graph_->failed_count();
                total = plan_graph_->total_count();
            } else {
                total = static_cast<int>(current_plan_.steps.size());
                for (const auto& step : current_plan_.steps) {
                    if (step.status == u8str(u8"completed")) ++completed;
                    else if (step.status == u8str(u8"failed")) ++failed;
                }
            }
            if (failed == 0 && completed == total) {
                execution_log_->final_status = u8str(u8"completed");
            } else if (completed > 0) {
                execution_log_->final_status = u8str(u8"partial");
            } else {
                execution_log_->final_status = u8str(u8"failed");
            }
        }

        // 如果已暂停等待用户输入，不覆盖 WaitingUserConfirm 状态
        if (!paused_for_user_input_) {
            set_state(AgentState::Completed);
        }

    } catch (const std::bad_alloc& e) {
        PE_ERROR("bad_alloc: " << e.what());
        set_final_output(u8str(u8"[PlanExecuteLoop Error: bad_alloc]"));
        set_state(AgentState::Error);
    } catch (const std::exception& e) {
        PE_ERROR("exception: " << e.what());
        set_final_output(u8str(u8"[PlanExecuteLoop Error]"));
        set_state(AgentState::Error);
    }
}

// Parse text-based tool calls from LLM response content.
// Some LLMs (e.g. GLM) output tool calls as markdown code blocks like:
//   ```tool
//   cloak_launch
//   ```
// This function detects such patterns and converts them to real ToolCall objects.
static std::vector<ToolCall> parse_text_tool_calls(const u8str& content, int next_id, bool debug) {
    std::vector<ToolCall> result;
    std::string text(content.begin(), content.end());

    // Pattern 1: ```tool\nTOOL_NAME\n``` or ```tool_call\nTOOL_NAME\n```
    // Pattern 2: ```tool\nTOOL_NAME(args)\n```
    // Pattern 3: ```tool_call\nTOOL_NAME(args)\n```

    const std::string markers[] = {"```tool", "``tool", "`tool"};
    const std::string closing_marker = "```";

    for (const auto& marker : markers) {
        size_t pos = 0;
        while ((pos = text.find(marker, pos)) != std::string::npos) {
            size_t line_start = text.find('\n', pos);
            if (line_start == std::string::npos) {
                pos += 1;
                continue;
            }
            line_start += 1; // skip '\n'

            size_t line_end = text.find('\n', line_start);
            if (line_end == std::string::npos) {
                line_end = text.find(closing_marker, line_start);
                if (line_end == std::string::npos) break;
            }

            std::string tool_line = text.substr(line_start, line_end - line_start);
            // Trim whitespace
            size_t first = tool_line.find_first_not_of(" \t\r");
            if (first == std::string::npos) { pos = line_end + 1; continue; }
            size_t last = tool_line.find_last_not_of(" \t\r");
            tool_line = tool_line.substr(first, last - first + 1);

            // Ignore common non-tool lines
            if (tool_line.empty() || tool_line == "tool" || tool_line == "tool_call" || tool_line.find(' ') != std::string::npos) {
                pos = line_end + 1;
                continue;
            }

            // Find closing marker
            size_t close_pos = text.find(closing_marker, line_end);
            if (close_pos == std::string::npos) { pos = line_end + 1; continue; }

            // Extract the tool name and optional args
            std::string tool_name;
            std::string tool_args = "{}";
            size_t paren_open = tool_line.find('(');
            if (paren_open != std::string::npos) {
                tool_name = tool_line.substr(0, paren_open);
                size_t paren_close = tool_line.find(')', paren_open);
                if (paren_close != std::string::npos) {
                    std::string args_str = tool_line.substr(paren_open + 1, paren_close - paren_open - 1);
                    // Try to parse as JSON or construct as JSON
                    if (!args_str.empty()) {
                        try {
                            // 先清理 UTF-8，防止 json::parse 抛出 type_error.316
                            u8str clean_args(args_str.begin(), args_str.end());
                            clean_args = llm::sanitize_utf8(clean_args);
                            std::string clean_str(clean_args.begin(), clean_args.end());
                            nlohmann::json j = nlohmann::json::parse(clean_str);
                            tool_args = j.dump();
                        } catch (...) {
                            if (debug) {
                                AGENT_LOG_DEBUG("PlanExec") << "JSON parse failed for tool args, falling back to key=value format";
                            }
                            // Convert key=value format to JSON
                            nlohmann::json j;
                            size_t eq_pos = args_str.find('=');
                            if (eq_pos != std::string::npos) {
                                std::string key = args_str.substr(0, eq_pos);
                                std::string val = args_str.substr(eq_pos + 1);
                                // Trim
                                size_t kf = key.find_first_not_of(" \t");
                                size_t kl = key.find_last_not_of(" \t");
                                if (kf != std::string::npos) key = key.substr(kf, kl - kf + 1);
                                size_t vf = val.find_first_not_of(" \t");
                                size_t vl = val.find_last_not_of(" \t");
                                if (vf != std::string::npos) val = val.substr(vf, vl - vf + 1);
                                // Remove quotes if present
                                if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                                    val = val.substr(1, val.size() - 2);
                                } else if (val == "true" || val == "false") {
                                    j[key] = (val == "true");
                                } else {
                                    try { j[key] = std::stoi(val); }
                                    catch (...) { j[key] = val; }
                                }
                            }
                            tool_args = j.dump();
                        }
                    }
                }
            } else {
                tool_name = tool_line;
            }

            // Trim tool_name
            size_t nf = tool_name.find_first_not_of(" \t");
            size_t nl = tool_name.find_last_not_of(" \t");
            if (nf != std::string::npos) tool_name = tool_name.substr(nf, nl - nf + 1);

            if (tool_name.empty()) { pos = close_pos + 1; continue; }

            // Map common short names to full MCP names
            if (debug) {
                AGENT_LOG_DEBUG("PlanExec") << "Text-parsed tool call: \"" << tool_name << "\" args=" << tool_args;
            }

            ToolCall tc;
            int id = next_id++;  // increment for the next possible call
            std::string id_str = std::to_string(id);
            tc.id = u8str(id_str.begin(), id_str.end());
            tc.name = u8str(tool_name.begin(), tool_name.end());
            tc.arguments = u8str(tool_args.begin(), tool_args.end());
            result.push_back(std::move(tc));

            pos = close_pos + 1;
        }
    }

    // Pattern 4: DSML format
    //   <|...|tool_calls>
    //     <|...|invoke name="tool_name">
    //       <|...|parameter name="param_name">param_value</|...|parameter>
    //     </|...|invoke>
    //   </|...|tool_calls>
    // The delimiters |...| can be |, ｜ (U+FF5C), or other characters
    {
        size_t search_pos = 0;
        while (true) {
            // Find <...tool_calls> block
            size_t block_start = text.find('<', search_pos);
            if (block_start == std::string::npos) break;
            // Find the closing > of the tool_calls open tag
            size_t tag_end = text.find('>', block_start);
            if (tag_end == std::string::npos) { search_pos = block_start + 1; continue; }
            std::string open_tag = text.substr(block_start, tag_end - block_start);
            // Check if this is a tool_calls tag (contains "tool_calls" and is not a closing tag)
            if (open_tag.find("tool_calls") == std::string::npos || open_tag.find('/') != std::string::npos) {
                search_pos = tag_end + 1;
                continue;
            }
            // Find the closing </...tool_calls>
            size_t close_block = text.find("</", tag_end);
            if (close_block == std::string::npos) { search_pos = tag_end + 1; continue; }
            size_t close_tag_end = text.find('>', close_block);
            if (close_tag_end == std::string::npos) { search_pos = tag_end + 1; continue; }
            std::string close_tag = text.substr(close_block, close_tag_end - close_block);
            if (close_tag.find("tool_calls") == std::string::npos) {
                search_pos = tag_end + 1;
                continue;
            }

            // Extract content between tool_calls tags
            std::string inner = text.substr(tag_end + 1, close_block - tag_end - 1);

            // Parse invoke blocks inside
            size_t invoke_pos = 0;
            while ((invoke_pos = inner.find("<", invoke_pos)) != std::string::npos) {
                size_t invoke_tag_end = inner.find('>', invoke_pos);
                if (invoke_tag_end == std::string::npos) break;
                std::string invoke_tag = inner.substr(invoke_pos, invoke_tag_end - invoke_pos);
                // Check if this is an invoke tag (contains "invoke" and has name= attribute)
                if (invoke_tag.find("invoke") == std::string::npos ||
                    invoke_tag.find("name") == std::string::npos ||
                    invoke_tag.find('/') != std::string::npos) {
                    invoke_pos = invoke_tag_end + 1;
                    continue;
                }
                // Extract tool name from name="..." or name='...'
                std::string tool_name;
                size_t name_eq = invoke_tag.find("name=");
                if (name_eq != std::string::npos) {
                    name_eq += 5; // skip "name="
                    char quote = invoke_tag[name_eq];
                    if (quote == '"' || quote == '\'') {
                        size_t name_end = invoke_tag.find(quote, name_eq + 1);
                        if (name_end != std::string::npos) {
                            tool_name = invoke_tag.substr(name_eq + 1, name_end - name_eq - 1);
                        }
                    }
                }
                if (tool_name.empty()) {
                    invoke_pos = invoke_tag_end + 1;
                    continue;
                }

                // Find matching </...invoke> closing tag
                std::string invoke_close_marker = "</";
                size_t invoke_close = inner.find(invoke_close_marker, invoke_tag_end);
                if (invoke_close == std::string::npos) {
                    invoke_pos = invoke_tag_end + 1;
                    continue;
                }
                size_t invoke_close_end = inner.find('>', invoke_close);
                if (invoke_close_end == std::string::npos) {
                    invoke_pos = invoke_tag_end + 1;
                    continue;
                }
                std::string invoke_close_tag = inner.substr(invoke_close, invoke_close_end - invoke_close);
                if (invoke_close_tag.find("invoke") == std::string::npos) {
                    invoke_pos = invoke_tag_end + 1;
                    continue;
                }

                // Extract content between invoke open and close tags
                std::string invoke_inner = inner.substr(invoke_tag_end + 1, invoke_close - invoke_tag_end - 1);

                // Parse parameter tags inside invoke
                nlohmann::json args_json = nlohmann::json::object();
                size_t param_pos = 0;
                while ((param_pos = invoke_inner.find("<", param_pos)) != std::string::npos) {
                    size_t param_tag_end = invoke_inner.find('>', param_pos);
                    if (param_tag_end == std::string::npos) break;
                    std::string param_tag = invoke_inner.substr(param_pos, param_tag_end - param_pos);
                    if (param_tag.find("parameter") == std::string::npos ||
                        param_tag.find("name") == std::string::npos ||
                        param_tag.find('/') != std::string::npos) {
                        param_pos = param_tag_end + 1;
                        continue;
                    }
                    // Extract parameter name
                    std::string param_name;
                    size_t pname_eq = param_tag.find("name=");
                    if (pname_eq != std::string::npos) {
                        pname_eq += 5;
                        char quote = param_tag[pname_eq];
                        if (quote == '"' || quote == '\'') {
                            size_t pname_end = param_tag.find(quote, pname_eq + 1);
                            if (pname_end != std::string::npos) {
                                param_name = param_tag.substr(pname_eq + 1, pname_end - pname_eq - 1);
                            }
                        }
                    }
                    if (param_name.empty()) {
                        param_pos = param_tag_end + 1;
                        continue;
                    }

                    // Find parameter value (content between open and close tags)
                    std::string param_close_marker = "</";
                    size_t param_close = invoke_inner.find(param_close_marker, param_tag_end);
                    if (param_close == std::string::npos) {
                        param_pos = param_tag_end + 1;
                        continue;
                    }
                    size_t param_close_end = invoke_inner.find('>', param_close);
                    if (param_close_end == std::string::npos) {
                        param_pos = param_tag_end + 1;
                        continue;
                    }

                    std::string param_value = invoke_inner.substr(param_tag_end + 1, param_close - param_tag_end - 1);
                    args_json[param_name] = param_value;

                    param_pos = param_close_end + 1;
                }

                // Create ToolCall
                ToolCall tc;
                int id = next_id++;
                std::string id_str = std::to_string(id);
                tc.id = u8str(id_str.begin(), id_str.end());
                tc.name = u8str(tool_name.begin(), tool_name.end());
                tc.arguments = u8str(args_json.dump().begin(), args_json.dump().end());
                result.push_back(std::move(tc));

                invoke_pos = invoke_close_end + 1;
            }

            search_pos = close_tag_end + 1;
        }
    }

    return result;
}

void PlanExecuteLoop::execute_single_step(const u8str& step_prompt, u8str& system_prompt) {
    u8str last_content;
    int text_only_retry_count = 0;
    const int kMaxTextRetries = 3;
    bool tool_was_called_this_step = false;

    for (int step = 0; step < config_.base.max_steps; ++step) {
        if (interrupted_.load() || stopped_.load()) {
            PE_DEBUG("execute_single_step: interrupted or stopped");
            return;
        }

        LlmRequest request;
        request.messages = context_.get_messages();
        {
            size_t total_size = 0;
            for (const auto& msg : request.messages) {
                total_size += msg.content.size();
            }
            PE_DEBUG("[PlanExec:size] context messages: " << request.messages.size()
                      << " msgs, " << total_size << " bytes total");
        }
        request.system_prompt = system_prompt;
        request.tools = cached_tools_schema_;

        PE_DEBUG("execute_single_step: calling LLM (inner step " << (step + 1) << "/" << config_.base.max_steps
                  << ", tools available: " << request.tools.size() << ")");

        LlmResponse response = call_llm_for_execute(request);

        if (response.is_error) {
            PE_ERROR("LLM error: " << std::string(response.error_message.begin(), response.error_message.end()));
            final_output_ = u8str(u8"[LLM Error] ") + response.error_message;
            set_state(AgentState::Error);
            return;
        }

        if (!response.content.empty()) {
            last_content = response.content;
        }

        std::string content_snippet(response.content.begin(), response.content.end());
        if (content_snippet.length() > 120) {
            content_snippet = content_snippet.substr(0, 120) + "...";
        }
        PE_DEBUG("LLM response: tool_calls=" << response.tool_calls.size()
                  << ", content=\"" << content_snippet << "\"");

        if (config_.base.enable_thinking) {
            ThinkingStep thinking_step;
            if (response.thinking_content) {
                thinking_step.thinking_content = std::move(*response.thinking_content);
            } else {
                thinking_step.thinking_content = response.content;
            }
            thinking_step.timestamp = std::chrono::system_clock::now();
            add_thinking_step(std::move(thinking_step));
        }

        // Try to parse text-based tool calls if LLM didn't use structured function calls
        if (response.tool_calls.empty()) {
            auto text_calls = parse_text_tool_calls(response.content, 1, config_.base.debug);
            if (!text_calls.empty()) {
                PE_DEBUG("Found " << text_calls.size() << " text-based tool call(s)");
                std::unordered_map<std::string, std::string> name_map;
                try {
                    auto mcps = mcps_.list_mcps();
                    for (const auto& mcp : mcps) {
                        std::string server_str(mcp->name().begin(), mcp->name().end());
                        auto tools = mcp->list_tools();
                        for (const auto& tool : tools) {
                            if (!tool.contains("name")) continue;
                            std::string raw_name = tool["name"].get<std::string>();
                            std::string flat_name = server_str + "_" + raw_name;
                            name_map[raw_name] = flat_name;
                            name_map[flat_name] = flat_name;
                        }
                    }
                } catch (...) {
                    PE_DEBUG("Failed to list MCP tools for name mapping");
                }
                auto native_tools = tools_.list_tools();
                for (const auto& tool_ptr : native_tools) {
                    u8str tn_u8 = tool_ptr->name();
                    std::string tn(tn_u8.begin(), tn_u8.end());
                    name_map[tn] = tn;
                }

                for (auto& tc : text_calls) {
                    std::string raw_name(tc.name.begin(), tc.name.end());
                    auto it = name_map.find(raw_name);
                    if (it != name_map.end()) {
                        tc.name = u8str(it->second.begin(), it->second.end());
                        PE_DEBUG("Mapped tool name: \"" << raw_name << "\" -> \"" << it->second << "\"");
                    } else {
                        PE_DEBUG("Tool name \"" << raw_name << "\" not found in registered tools, using as-is");
                    }
                }
                response.tool_calls = std::move(text_calls);
            }
        }

        if (!response.tool_calls.empty()) {
            tool_was_called_this_step = true;
            text_only_retry_count = 0;
            context_.add_assistant_message(response.content, response.tool_calls, response.thinking_content);

            // 检查是否有工具需要用户确认
            bool any_needs_confirm = false;
            for (const auto& tc : response.tool_calls) {
                if (needs_confirmation(tc)) {
                    any_needs_confirm = true;
                    break;
                }
            }

            if (any_needs_confirm || response.tool_calls.size() <= 1) {
                // 串行执行：需要用户确认，或只有单个工具（并行无收益）
                for (const auto& tool_call : response.tool_calls) {
                    std::string tc_name(tool_call.name.begin(), tool_call.name.end());
                    std::string tc_args(tool_call.arguments.begin(), tool_call.arguments.end());
                    PE_DEBUG("Tool call: " << tc_name << "(" << tc_args.substr(0, 200) << ")");

                    if (needs_confirmation(tool_call)) {
                        set_state(AgentState::WaitingUserConfirm);
                        if (confirm_handler_) {
                            ConfirmRequest confirm_req;
                            confirm_req.action_description = tool_call.name;
                            confirm_req.details = tool_call.arguments;
                            ConfirmResult result = confirm_handler_->confirm(confirm_req);
                            if (!result.confirmed) {
                                set_state(AgentState::Thinking);
                                ToolResult denied_result;
                                denied_result.tool_call_id = tool_call.id;
                                denied_result.content = u8str(u8"User denied");
                                denied_result.is_error = true;
                                context_.add_tool_message(denied_result.tool_call_id, denied_result.content);

                                ThinkingStep denied_step;
                                denied_step.tool_call = tool_call;
                                denied_step.tool_result = denied_result;
                                denied_step.timestamp = std::chrono::system_clock::now();
                                add_thinking_step(std::move(denied_step));
                                continue;
                            }
                        }
                        set_state(AgentState::Thinking);
                    }

                    set_state(AgentState::WaitingToolResult);
                    ToolResult tool_result = execute_tool(tool_call);
                    set_state(AgentState::Thinking);

                    std::string tr_content(tool_result.content.begin(), tool_result.content.end());
                    PE_DEBUG("Tool result: is_error=" << tool_result.is_error
                              << ", content=\"" << tr_content.substr(0, 200) << "\"");

                    context_.add_tool_message(tool_result.tool_call_id, tool_result.content);

                    ThinkingStep tool_step;
                    tool_step.tool_call = tool_call;
                    tool_step.tool_result = tool_result;
                    tool_step.timestamp = std::chrono::system_clock::now();
                    add_thinking_step(std::move(tool_step));
                }
            } else {
                // 并行执行：所有工具均为 auto_confirm，同时启动
                PE_DEBUG("Executing " << response.tool_calls.size() << " tools in parallel");
                set_state(AgentState::WaitingToolResult);

                std::vector<std::future<ToolResult>> futures;
                futures.reserve(response.tool_calls.size());
                for (const auto& tc : response.tool_calls) {
                    std::string tc_name(tc.name.begin(), tc.name.end());
                    PE_DEBUG("Launching tool: " << tc_name);
                    futures.push_back(std::async(std::launch::async, [this, &tc]() {
                        return execute_tool(tc);
                    }));
                }

                set_state(AgentState::Thinking);

                // 按原始顺序收集结果，确保 context 中的 tool message 顺序一致
                for (size_t i = 0; i < response.tool_calls.size(); ++i) {
                    const auto& tool_call = response.tool_calls[i];
                    ToolResult tool_result = futures[i].get();

                    std::string tr_content(tool_result.content.begin(), tool_result.content.end());
                    std::string tc_name(tool_call.name.begin(), tool_call.name.end());
                    PE_DEBUG("Tool result [" << tc_name << "]: is_error=" << tool_result.is_error
                              << ", content=\"" << tr_content.substr(0, 200) << "\"");

                    context_.add_tool_message(tool_result.tool_call_id, tool_result.content);

                    ThinkingStep tool_step;
                    tool_step.tool_call = tool_call;
                    tool_step.tool_result = tool_result;
                    tool_step.timestamp = std::chrono::system_clock::now();
                    add_thinking_step(std::move(tool_step));
                }
            }

            // 处理 load_mcp_tool 调用，动态激活 MCP 工具并更新 schema / system prompt
            if (process_load_mcp_tool(response.tool_calls, system_prompt, execution_instruction())) {
                cached_tools_schema_ = build_combined_tools_schema();
            }

            continue;
        }

        // No tool_calls in response - LLM provided text only
        context_.add_assistant_message(response.content);

        // KEY FIX: If a tool was called this step and now LLM gives text,
        // it's a step completion summary (e.g. "STEP_DONE: browser launched").
        // Accept it immediately without retries.
        if (tool_was_called_this_step) {
            PE_DEBUG("Tool was called this step, accepting text response as step summary");
            set_final_output(response.content);
            return;
        }

        text_only_retry_count++;

        // Also detect STEP_DONE/STEP_FAILED in text even without prior tool call
        bool has_step_done = response.content.find(u8str(u8"STEP_DONE")) != u8str::npos;
        bool has_step_failed = response.content.find(u8str(u8"STEP_FAILED")) != u8str::npos;
        if (has_step_done || has_step_failed) {
            PE_DEBUG("Text response contains STEP_DONE or STEP_FAILED, accepting");
            set_final_output(response.content);
            return;
        }

        if (text_only_retry_count < kMaxTextRetries) {
            std::string msg = "You must call a tool to perform this step, not just describe it. "
                "Use the function calling format (tool_call) to actually perform the action. "
                "A text description without a tool call does nothing.";
            PE_DEBUG("No tool_call in response. "
                      << "Retry " << text_only_retry_count << "/" << kMaxTextRetries);

            if (text_only_retry_count == 0) {
                context_.compress();
            }
            context_.add_user_message(u8str(msg.begin(), msg.end()));
            continue;
        }

        // Exhausted retries, accept text output
        PE_DEBUG("WARNING: Exhausted " << kMaxTextRetries << " retries, "
                  << "accepting text response without tool_call");

        set_final_output(response.content);
        return;
    }

    // Max steps reached within a single plan step
    PE_DEBUG("execute_single_step: max steps (" << config_.base.max_steps << ") reached");
    if (last_content.empty()) {
        PE_DEBUG("Requesting final step summary after max_steps");
        context_.add_user_message(u8str(u8"You have reached the maximum number of reasoning steps for this plan step. "
            u8"Please provide a one-sentence factual summary based on the tool results above. "
            u8"Do not call any tools; output the summary directly."));

        LlmRequest final_request;
        final_request.messages = context_.get_messages();
        final_request.system_prompt = system_prompt;
        // 不传递 tools，强制生成纯文本总结

        try {
            LlmResponse final_response = call_llm_for_execute(final_request);
            if (!final_response.is_error && !final_response.content.empty()) {
                last_content = final_response.content;
                context_.add_assistant_message(final_response.content);
            } else if (final_response.is_error) {
                PE_ERROR("Final summary LLM error: "
                    << std::string(final_response.error_message.begin(), final_response.error_message.end()));
            }
        } catch (const std::exception& e) {
            PE_ERROR("Final summary LLM call error: " << e.what());
        }
    }
    {
        std::lock_guard<std::mutex> lock(output_mutex_);
        if (!last_content.empty()) {
            final_output_ = last_content;
        }
    }
}

// 构建步骤执行 prompt（含 tool_hint / tool_args_hint）
u8str PlanExecuteLoop::build_step_prompt(const PlanStep& step, int step_idx, int total) const {
    std::string step_id_str(step.id.begin(), step.id.end());
    std::string step_num_str = std::to_string(step_idx + 1);
    std::string total_str = std::to_string(total);

    u8str prompt = u8str(u8"[Executing Plan Step ")
        + u8str(step_num_str.begin(), step_num_str.end())
        + u8str(u8"/") + u8str(total_str.begin(), total_str.end())
        + u8str(u8"]: ") + step.description;

    if (step.tool_hint) {
        prompt += u8str(u8"\n\nSuggested tool: ") + *step.tool_hint;
    }
    if (step.tool_args_hint) {
        prompt += u8str(u8"\nSuggested arguments: ") + *step.tool_args_hint;
    }
    prompt += u8str(u8"\n\nCall the appropriate tool(s) to perform this step. Respond with a tool_call.\n"
        "\n=== CRITICAL: Step Output Format ===\n"
        "After the tool call completes, provide ONLY a ONE-SENTENCE factual summary of what was done.\n"
        "Do NOT generate the final answer. Do NOT write comprehensive analysis.\n"
        "The system will synthesize all step results into a final answer at the end.\n"
        "Example good output: \"Retrieved A-share index data: Shanghai Composite +1.28%, Shenzhen Component +3.02%.\"\n"
        "Example bad output (too verbose): \"Today's A-share market showed strong performance with... (long analysis)\"");

    return prompt;
}

// 输出步骤进度
void PlanExecuteLoop::emit_step_progress(const PlanStep& step, int total, bool is_success) const {
    std::string step_id_str(step.id.begin(), step.id.end());
    std::string total_str = std::to_string(total);
    std::string desc_str(step.description.begin(), step.description.end());

    u8str progress;
    if (is_success) {
        progress = u8str(u8"\n\u2705 **Step ")
            + u8str(step_id_str.begin(), step_id_str.end())
            + u8str(u8"/") + u8str(total_str.begin(), total_str.end())
            + u8str(u8" \u5b8c\u6210**\uff1a") + u8str(desc_str.begin(), desc_str.end())
            + u8str(u8"\n");
    } else {
        progress = u8str(u8"\n\u274c **Step ")
            + u8str(step_id_str.begin(), step_id_str.end())
            + u8str(u8"/") + u8str(total_str.begin(), total_str.end())
            + u8str(u8" \u5931\u8d25**\uff1a") + u8str(desc_str.begin(), desc_str.end())
            + u8str(u8"\n");
    }

    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (on_output_ready_) {
            on_output_ready_(progress);
        }
    }
}

// 设置最终输出并通知回调（封装 output_mutex_ + callback_mutex_ 锁对）
void PlanExecuteLoop::set_final_output(const u8str& output) {
    {
        std::lock_guard<std::mutex> lock(output_mutex_);
        final_output_ = output;
    }
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (on_output_ready_) {
            on_output_ready_(output);
        }
    }
}

// 统一处理步骤结果（校验、重试、重规划、进度输出、日志记录）
PlanExecuteLoop::StepAction PlanExecuteLoop::process_step_result(PlanStep& step, int step_idx, int total) {
    if (!final_output_.has_value()) {
        return StepAction::Continue;
    }

    u8str step_result = final_output_.value();
    step.result = step_result;
    step.end_time = std::chrono::system_clock::now();

    if (needs_user_input()) {
        step.status = u8str(u8"pending");
        PE_DEBUG("Step " << (step_idx + 1) << " paused, waiting for user input");
        return StepAction::Pause;
    }

    StepValidation validation = validate_step_result(step, step_result);

    if (validation == StepValidation::Fail) {
        if (step.retry_count < config_.max_step_retries) {
            // 记录重试日志
            if (execution_log_) {
                PlanStepLog retry_log;
                retry_log.step_id = step.id;
                retry_log.description = step.description;
                retry_log.status = u8str(u8"retry");
                retry_log.start_time = step.start_time;
                retry_log.end_time = step.end_time;
                auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
                    retry_log.end_time - retry_log.start_time);
                retry_log.duration_ms = static_cast<int>(dur.count());
                retry_log.validation_result = validation;
                retry_log.retry_count = step.retry_count;
                retry_log.is_retry = true;
                execution_log_->step_logs.push_back(std::move(retry_log));
            }
            step.retry_count++;
            PE_DEBUG("Step " << (step_idx + 1) << " retry " << step.retry_count
                      << "/" << config_.max_step_retries);
            // 图执行路径需要清除失败状态
            if (plan_graph_) {
                plan_graph_->clear_failed(step.id);
            }
            {
                std::lock_guard<std::mutex> lock(output_mutex_);
                final_output_.reset();
            }
            return StepAction::Retry;
        }

        // 重试次数用尽，标记失败
        step.status = u8str(u8"failed");
        if (plan_graph_) {
            plan_graph_->mark_failed(step.id);
        }
        PE_ERROR("Step " << (step_idx + 1) << " FAILED (after " << step.retry_count << " retries)");

        emit_step_progress(step, total, false);

        // 尝试重规划
        if (config_.replan_config.replan_on_failure) {
            std::string fail_reason = "Step failed after " + std::to_string(step.retry_count) + " retries";
            u8str reason(fail_reason.begin(), fail_reason.end());
            if (replan(step.id, reason)) {
                {
                    std::lock_guard<std::mutex> lock(output_mutex_);
                    final_output_.reset();
                }
                return StepAction::ReplanFromStart;
            }
        }
    } else {
        // Pass 或 Uncertain，标记完成
        step.status = u8str(u8"completed");
        if (plan_graph_) {
            plan_graph_->mark_completed(step.id);
        }
        PE_DEBUG("Step " << (step_idx + 1) << " completed");

        emit_step_progress(step, total, true);
    }

    // 记录到执行日志
    if (execution_log_) {
        PlanStepLog log_entry;
        log_entry.step_id = step.id;
        log_entry.description = step.description;
        log_entry.status = step.status;
        log_entry.start_time = step.start_time;
        log_entry.end_time = step.end_time;
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(step.end_time - step.start_time);
        log_entry.duration_ms = static_cast<int>(duration.count());
        log_entry.validation_result = validation;
        log_entry.retry_count = step.retry_count;
        execution_log_->step_logs.push_back(std::move(log_entry));
    }

    return StepAction::Continue;
}

void PlanExecuteLoop::interrupt(const u8str& new_input) {
    interrupted_ = true;
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        pending_inputs_.push(new_input);
    }
}

void PlanExecuteLoop::stop() {
    stopped_ = true;
    interrupted_ = false;
}

std::optional<Plan> PlanExecuteLoop::get_plan() const {
    return current_plan_;
}

bool PlanExecuteLoop::should_auto_continue() const {
    auto output = get_final_output();
    // 如果输出需要用户输入，不自动继续
    if (output && u8str_util::needs_user_input(*output)) return false;
    // 检查 plan 步骤是否全部完成
    if (current_plan_.steps.empty()) return false;
    for (const auto& step : current_plan_.steps) {
        if (step.status != u8str(u8"completed") && step.status != u8str(u8"failed")
            && step.status != u8str(u8"skipped")) {
            return true;  // 还有未完成的步骤（包括重规划后的新步骤）
        }
    }
    return false;  // 所有步骤都已完成
}

bool PlanExecuteLoop::needs_user_input() const {
    // 1. WaitingUserConfirm 状态 = 明确需要用户
    if (get_state() == AgentState::WaitingUserConfirm) return true;
    // 2. 步骤暂停等待输入
    if (paused_for_user_input_) return true;
    // 3. 也检查输出文本模式（兼容）
    auto output = get_final_output();
    if (output && u8str_util::needs_user_input(*output)) return true;
    return false;
}

std::optional<PlanExecutionLog> PlanExecuteLoop::get_execution_log() const {
    return execution_log_;
}

LlmResponse PlanExecuteLoop::call_llm_for_plan(const LlmRequest& request) {
    // 使用初始化语法而非先默认构造再赋值，便于编译器 NRVO 优化
    LlmResponse response = planner_llm_ ? planner_llm_->send_request(request)
                                        : llm_provider_->send_request(request);
    record_token_usage(response);
    return response;
}

LlmResponse PlanExecuteLoop::call_llm_for_execute(const LlmRequest& request) {
    LlmResponse response = executor_llm_ ? executor_llm_->send_request(request)
                                         : llm_provider_->send_request(request);
    record_token_usage(response);
    return response;
}

ToolResult PlanExecuteLoop::execute_tool(const ToolCall& tool_call) {
    ToolResult result;
    result.tool_call_id = tool_call.id;
    ToolPtr tool = tools_.get_tool(tool_call.name);
    if (tool) {
        try {
            result.content = llm::sanitize_utf8(tool->execute(tool_call.arguments));
            result.is_error = false;
        } catch (const std::bad_alloc& e) {
            PE_ERROR("bad_alloc native tool execute: " << e.what());
            result.content = u8str(u8"[bad_alloc native tool] ") + u8str_util::to_u8str(std::string(e.what()));
            result.is_error = true;
        } catch (const std::exception& e) {
            result.content = u8str_util::to_u8str(std::string(e.what()));
            result.is_error = true;
        }
    } else {
        std::string name_str(tool_call.name.begin(), tool_call.name.end());
        std::string server_name;
        std::string tool_name;
        McpPtr       resolved_mcp;

        if (name_str.find("mcp__") == 0) {
            auto rest = name_str.substr(5);
            auto sep = rest.find("__");
            if (sep != std::string::npos) {
                server_name = rest.substr(0, sep);
                tool_name = rest.substr(sep + 2);
                resolved_mcp = mcps_.get_mcp(u8str_util::to_u8str(server_name));
            }
        }
        if (!resolved_mcp) {
            auto sep_pos = name_str.find('_');
            if (sep_pos != std::string::npos) {
                server_name = name_str.substr(0, sep_pos);
                tool_name = name_str.substr(sep_pos + 1);
                resolved_mcp = mcps_.get_mcp(u8str_util::to_u8str(server_name));
            }
        }
        if (!resolved_mcp) {
            auto dot_sep = name_str.find('.');
            if (dot_sep != std::string::npos) {
                server_name = name_str.substr(0, dot_sep);
                tool_name = name_str.substr(dot_sep + 1);
                resolved_mcp = mcps_.get_mcp(u8str_util::to_u8str(server_name));
            }
        }
        if (!resolved_mcp) {
            auto mcps = mcps_.list_mcps();
            for (const auto& mcp : mcps) {
                auto sname = mcp->name();
                std::string sname_str(sname.begin(), sname.end());
                auto tools = mcp->list_tools();
                for (const auto& t : tools) {
                    if (t.contains("name") && t["name"].get<std::string>() == name_str) {
                        server_name = sname_str;
                        tool_name = name_str;
                        resolved_mcp = mcp;
                        break;
                    }
                }
                if (resolved_mcp) break;
            }
        }

        if (resolved_mcp) {
            u8str args = tool_call.arguments;
            try {
                u8str method_u8(tool_name.begin(), tool_name.end());
                result.content = resolved_mcp->call(method_u8, args);
                result.is_error = false;
            } catch (const std::bad_alloc& e) {
                result.content = u8str_util::to_u8str(std::string(e.what()));
                result.is_error = true;
                PE_ERROR("bad_alloc MCP call failed for " << name_str);
            } catch (const std::exception& e) {
                result.content = u8str_util::to_u8str(std::string(e.what()));
                result.is_error = true;
                PE_ERROR("execute_tool: MCP call failed for " << name_str << ": " << e.what());
            }
            return result;
        }

        PE_ERROR("execute_tool: MCP not found for '" << name_str << "'");
        result.content = u8str(u8"Tool not found: ") + tool_call.name;
        result.is_error = true;
    }
    return result;
}

bool PlanExecuteLoop::needs_confirmation(const ToolCall& tool_call) const {
    if (config_.base.auto_confirm) {
        return false;
    }
    ToolPtr tool = tools_.get_tool(tool_call.name);
    if (tool) {
        if (tool->requires_confirmation()) {
            return true;
        }
    }
    // 所有工具（包括 MCP）都检查危险命令模式
    if (u8str_util::is_dangerous_command(tool_call.arguments)) {
        return true;
    }
    return false;
}

Plan PlanExecuteLoop::parse_plan(const u8str& llm_output) const {
    // 优先尝试 JSON 格式解析
    Plan json_plan = try_parse_json_plan(llm_output);
    if (!json_plan.steps.empty()) {
        return json_plan;
    }
    // 降级到纯文本解析
    return parse_text_plan(llm_output);
}

Plan PlanExecuteLoop::try_parse_json_plan(const u8str& llm_output) const {
    Plan plan;
    std::string output(llm_output.begin(), llm_output.end());

    // 查找 PLAN: ... PLAN_END 区域
    std::string json_text;
    size_t plan_start = output.find("PLAN:");
    if (plan_start != std::string::npos) {
        plan_start += 5;
        size_t plan_end = output.find("PLAN_END", plan_start);
        if (plan_end != std::string::npos) {
            json_text = output.substr(plan_start, plan_end - plan_start);
        } else {
            json_text = output.substr(plan_start);
        }
    } else {
        json_text = output;
    }

    // Trim whitespace
    size_t start = json_text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return plan;
    json_text = json_text.substr(start);

    // 尝试解析 JSON
    try {
        auto j = nlohmann::json::parse(json_text);
        if (!j.is_array()) return plan;

        for (const auto& item : j) {
            PlanStep step;
            if (item.contains("id")) {
                auto id_val = item["id"];
                if (id_val.is_string()) {
                    std::string id_str = id_val.get<std::string>();
                    step.id = u8str(id_str.begin(), id_str.end());
                } else if (id_val.is_number()) {
                    std::string id_str = std::to_string(id_val.get<int>());
                    step.id = u8str(id_str.begin(), id_str.end());
                }
            }
            if (step.id.empty()) {
                std::string id_str = std::to_string(static_cast<int>(plan.steps.size()) + 1);
                step.id = u8str(id_str.begin(), id_str.end());
            }

            if (item.contains("description") && item["description"].is_string()) {
                std::string desc = item["description"].get<std::string>();
                step.description = u8str(desc.begin(), desc.end());
            }
            if (step.description.empty()) continue;  // 跳过无描述的步骤

            step.status = u8str(u8"pending");

            if (item.contains("depends_on") && item["depends_on"].is_array()) {
                for (const auto& dep : item["depends_on"]) {
                    std::string dep_str;
                    if (dep.is_string()) {
                        dep_str = dep.get<std::string>();
                    } else if (dep.is_number()) {
                        dep_str = std::to_string(dep.get<int>());
                    }
                    if (!dep_str.empty()) {
                        step.depends_on.push_back(u8str(dep_str.begin(), dep_str.end()));
                    }
                }
            }

            if (item.contains("tool_hint") && item["tool_hint"].is_string()) {
                std::string hint = item["tool_hint"].get<std::string>();
                step.tool_hint = u8str(hint.begin(), hint.end());
            }

            if (item.contains("tool_args_hint") && item["tool_args_hint"].is_string()) {
                std::string args = item["tool_args_hint"].get<std::string>();
                step.tool_args_hint = u8str(args.begin(), args.end());
            }

            if (item.contains("expected_output") && item["expected_output"].is_string()) {
                std::string exp = item["expected_output"].get<std::string>();
                step.expected_output = u8str(exp.begin(), exp.end());
            }

            if (item.contains("condition") && item["condition"].is_string()) {
                std::string cond = item["condition"].get<std::string>();
                step.condition = u8str(cond.begin(), cond.end());
            }

            if (item.contains("fallback_step") && item["fallback_step"].is_string()) {
                std::string fb = item["fallback_step"].get<std::string>();
                step.fallback_step = u8str(fb.begin(), fb.end());
            }

            plan.steps.push_back(std::move(step));
        }
    } catch (const nlohmann::json::exception&) {
        // JSON 解析失败，返回空 plan（调用方会降级到文本解析）
        return Plan{};
    }

    return plan;
}

Plan PlanExecuteLoop::parse_text_plan(const u8str& llm_output) const {
    Plan plan;

    // Convert u8str to std::string (single allocation, required for downstream)
    std::string output;
    output.assign(reinterpret_cast<const char*>(llm_output.data()), llm_output.size());

    // Use string_view to find the PLAN section without extra allocations
    std::string_view view(output);
    std::string_view plan_view;

    size_t plan_start = view.find("PLAN:");
    if (plan_start != std::string_view::npos) {
        plan_start += 5;
        size_t plan_end = view.find("PLAN_END");
        if (plan_end != std::string_view::npos) {
            plan_view = view.substr(plan_start, plan_end - plan_start);
        } else {
            plan_view = view.substr(plan_start);
        }
    } else {
        plan_view = view;
    }

    int step_id = 1;
    size_t line_start = 0;

    while (line_start < plan_view.size()) {
        size_t line_end = plan_view.find('\n', line_start);
        if (line_end == std::string_view::npos) {
            line_end = plan_view.size();
        }

        std::string_view line = plan_view.substr(line_start, line_end - line_start);

        // Trim leading whitespace
        size_t content_start = 0;
        while (content_start < line.size() && (line[content_start] == ' ' || line[content_start] == '\t' || line[content_start] == '\r')) {
            ++content_start;
        }

        if (content_start < line.size()) {
            std::string_view trimmed = line.substr(content_start);

            // 检测多种 step 格式
            bool is_step = (trimmed.size() >= 5 && (trimmed.substr(0, 5) == "Step " || trimmed.substr(0, 5) == "step ")) ||
                           (trimmed.size() >= 2 && (trimmed.substr(0, 2) == "- " || trimmed.substr(0, 2) == "* "));

            if (!is_step && trimmed.size() >= 3) {
                size_t num_end = 0;
                while (num_end < trimmed.size() && trimmed[num_end] >= '0' && trimmed[num_end] <= '9') {
                    ++num_end;
                }
                if (num_end > 0 && num_end + 1 < trimmed.size()) {
                    char sep = trimmed[num_end];
                    if ((sep == '.' || sep == ')') && trimmed[num_end + 1] == ' ') {
                        is_step = true;
                    }
                }
            }

            if (is_step) {
                int extracted_id = -1;

                if (trimmed.size() >= 5 && (trimmed.substr(0, 5) == "Step " || trimmed.substr(0, 5) == "step ")) {
                    size_t num_start = 5;
                    while (num_start < trimmed.size() && trimmed[num_start] >= '0' && trimmed[num_start] <= '9') {
                        ++num_start;
                    }
                    if (num_start > 5) {
                        try { extracted_id = std::stoi(std::string(trimmed.substr(5, num_start - 5))); }
                        catch (...) {
                            PE_DEBUG("Failed to parse step number from prefix: " << std::string(trimmed.substr(5, num_start - 5)));
                        }
                    }
                }

                if (extracted_id < 0 && trimmed.size() >= 3) {
                    size_t num_end = 0;
                    while (num_end < trimmed.size() && trimmed[num_end] >= '0' && trimmed[num_end] <= '9') {
                        ++num_end;
                    }
                    if (num_end > 0 && num_end + 1 < trimmed.size()) {
                        char sep = trimmed[num_end];
                        if ((sep == '.' || sep == ')') && trimmed[num_end + 1] == ' ') {
                            try { extracted_id = std::stoi(std::string(trimmed.substr(0, num_end))); }
                            catch (...) {
                                PE_DEBUG("Failed to parse step number: " << std::string(trimmed.substr(0, num_end)));
                            }
                        }
                    }
                }

                if (extracted_id > 0) {
                    step_id = extracted_id;
                }

                size_t colon = trimmed.find(':');
                size_t dot = trimmed.find('.');
                size_t paren = trimmed.find(')');
                size_t separator = std::string_view::npos;

                if (colon != std::string_view::npos) separator = colon;
                if (dot != std::string_view::npos && (separator == std::string_view::npos || dot < separator)) separator = dot;
                if (paren != std::string_view::npos && (separator == std::string_view::npos || paren < separator)) separator = paren;

                if (separator != std::string_view::npos) {
                    std::string_view desc_view = trimmed.substr(separator + 1);
                    size_t desc_start = 0;
                    while (desc_start < desc_view.size() && desc_view[desc_start] == ' ') {
                        ++desc_start;
                    }
                    if (desc_start < desc_view.size()) {
                        desc_view = desc_view.substr(desc_start);
                    } else {
                        desc_view = std::string_view();
                    }

                    PlanStep step;
                    std::string id_str = std::to_string(step_id);
                    step.id = u8str(id_str.begin(), id_str.end());
                    step.description = u8str(reinterpret_cast<const char8_t*>(desc_view.data()), desc_view.size());
                    step.status = u8str(u8"pending");
                    plan.steps.push_back(std::move(step));
                    ++step_id;
                }
            }
        }

        line_start = line_end + 1;
    }

    return plan;
}

u8str PlanExecuteLoop::plan_execute_instruction() const {
    static const u8str kInstruction = u8str(u8"You are a Plan-and-Execute agent. You are in the PLANNING phase.\n"
        "\n"
        "ONLY create a plan. DO NOT call any tools. The system executes steps after planning.\n"
        "\n"
        "=== PLAN FORMAT (JSON, wrapped in PLAN:/PLAN_END markers) ===\n"
        "PLAN:\n"
        "[\n"
        "  {\n"
        "    \"id\": \"1\",\n"
        "    \"description\": \"what to do (required)\",\n"
        "    \"depends_on\": [],\n"
        "    \"tool_hint\": \"exact tool function name (e.g. servername_toolname)\",\n"
        "    \"tool_args_hint\": \"optional pre-filled JSON args\",\n"
        "    \"expected_output\": \"optional expected result\",\n"
        "    \"condition\": \"optional: if step_N succeeded / if step_N failed\",\n"
        "    \"fallback_step\": \"optional step id to jump to on failure\"\n"
        "  }\n"
        "]\n"
        "PLAN_END\n"
        "\n"
        "Rules:\n"
        "- Each step must be a single, focused action.\n"
        "- tool_hint must match an exact function name from available tools.\n"
        "- Steps with no dependencies can run in parallel (use depends_on for ordering).\n");
    return kInstruction;
}

u8str PlanExecuteLoop::execution_instruction() const {
    static const u8str kInstruction = u8str(u8"You are executing a single step from a pre-defined plan.\n"
        "\n"
        "=== CRITICAL RULE: You MUST use function calls (tool_calls) to perform actions ===\n"
        "Describing an action in text does NOTHING. Only a real function call executes the action.\n"
        "\n"
        "Available tools are provided via function calling. Each MCP sub-tool is a separate callable function.\n"
        "Call them directly by their full name (e.g. servername_toolname).\n"
        "Follow the tool's parameter schema exactly when making calls.\n"
        "\n"
        "=== Step Execution Flow ===\n"
        "1. If the step has a tool_hint, call that tool using tool_calls.\n"
        "2. If the step has tool_args_hint, use those as the starting arguments.\n"
        "3. Examine the tool result and call more tools if needed.\n"
        "4. When the step is complete, provide ONLY a ONE-SENTENCE factual summary.\n"
        "\n"
        "=== Important ===\n"
        "- Only execute the CURRENT step. Do not try to do multiple steps at once.\n"
        "- If a tool call fails, report the error and stop (the system will handle retries).\n"
        "- If you need information from the user, state your request clearly.\n"
        "- Do NOT generate the final answer. The system will synthesize all results at the end.\n"
        "- Keep your text output SHORT — one or two sentences maximum per step.\n");
    return kInstruction;
}

// Phase 3: 汇总所有完成步骤的结果，调用 LLM 生成最终综合答案
void PlanExecuteLoop::summarize_all_steps(u8str& final_output) {
    // 收集所有已完成的步骤描述和结果
    u8str synthesis;
    synthesis = u8str(u8"All plan steps have been completed. Here is a summary of what each step accomplished:\n\n");

    int step_num = 1;
    for (const auto& step : current_plan_.steps) {
        std::string num_str = std::to_string(step_num);
        synthesis += u8str(u8"Step ") + u8str(num_str.begin(), num_str.end()) + u8str(u8": ");
        synthesis += step.description + u8str(u8"\n");
        if (!step.result.empty()) {
            synthesis += u8str(u8"  Result: ") + step.result + u8str(u8"\n");
        }
        synthesis += u8str(u8"\n");
        ++step_num;
    }

    synthesis += u8str(u8"\n=== Your Task ===\n");
    synthesis += u8str(u8"Based on the step results above, synthesize a comprehensive final answer for the user.\n");
    synthesis += u8str(u8"Focus on answering the user's original question using ALL the data collected.\n");
    synthesis += u8str(u8"Use markdown formatting (tables, bullets, bold) for clarity.\n");
    synthesis += u8str(u8"Be concise but thorough — the user expects a complete answer.\n");
    synthesis += u8str(u8"Do NOT call any tools. This is a pure text synthesis task.\n");

    try {
        context_.add_user_message(synthesis);

        std::string syn_inst =
            "You are in the FINAL SUMMARIZATION phase. All step results are collected.\n"
            "Your ONLY job is to synthesize them into a clear, well-formatted final answer.\n"
            "Do NOT call any tools. Respond with text only.\n"
            "Use markdown tables, bullet points, and section headers as appropriate.";

        u8str syn_instruction(syn_inst.begin(), syn_inst.end());
        u8str syn_system_prompt = prompt_builder_.build_system_prompt(
            personality_docs_,
            syn_instruction
        );

        LlmRequest syn_request;
        syn_request.messages.push_back({MessageRole::System, syn_system_prompt});
        syn_request.messages.push_back({MessageRole::User, synthesis});
        syn_request.tools = nlohmann::json::array();  // 不让 LLM 调用工具

        auto syn_response = call_llm_for_execute(syn_request);
        if (!syn_response.is_error && !syn_response.content.empty()) {
            final_output = syn_response.content;
            PE_DEBUG("Phase 3 summarization complete");
        } else {
            PE_DEBUG("Phase 3 summarization skipped (no response or error)");
        }
    } catch (const std::exception& e) {
        PE_ERROR("Phase 3 summarization failed: " << e.what());
    }
}

bool PlanExecuteLoop::should_execute_step(const PlanStep& step) const {
    if (!step.condition) return true;  // 无条件，执行

    std::string cond(step.condition->begin(), step.condition->end());
    // 支持的模式：
    //   "if step_N succeeded" / "if step_N completed"
    //   "if step_N failed"
    //   "if step_N.result contains 'xxx'"

    // 提取步骤 ID
    std::string ref_step_id;
    size_t step_pos = cond.find("step_");
    if (step_pos != std::string::npos) {
        size_t num_start = step_pos + 5;
        size_t num_end = num_start;
        while (num_end < cond.size() && cond[num_end] >= '0' && cond[num_end] <= '9') {
            ++num_end;
        }
        ref_step_id = cond.substr(num_start, num_end - num_start);
    }
    if (ref_step_id.empty()) return true;

    // 查找引用步骤的状态
    if (!plan_graph_) return true;
    auto ref_step = plan_graph_->get_step(u8str(ref_step_id.begin(), ref_step_id.end()));
    if (!ref_step) return true;

    if (cond.find("succeeded") != std::string::npos || cond.find("completed") != std::string::npos) {
        return ref_step->status == u8str(u8"completed");
    }
    if (cond.find("failed") != std::string::npos) {
        return ref_step->status == u8str(u8"failed");
    }
    if (cond.find("contains") != std::string::npos) {
        // 提取引号内的内容
        size_t q1 = cond.find('\'');
        size_t q2 = cond.find('\"');
        size_t quote_start = std::string::npos;
        char quote_char = '\'';
        if (q1 != std::string::npos && (q2 == std::string::npos || q1 < q2)) {
            quote_start = q1;
            quote_char = '\'';
        } else if (q2 != std::string::npos) {
            quote_start = q2;
            quote_char = '\"';
        }
        if (quote_start != std::string::npos) {
            size_t quote_end = cond.find(quote_char, quote_start + 1);
            if (quote_end != std::string::npos) {
                std::string search_str = cond.substr(quote_start + 1, quote_end - quote_start - 1);
                return ref_step->result.find(u8str(search_str.begin(), search_str.end())) != u8str::npos;
            }
        }
    }

    return true;  // 未知条件，默认执行
}

StepValidation PlanExecuteLoop::validate_step_result(const PlanStep& step, const u8str& result) const {
    // 检查明显的错误模式
    if (result.find(u8str(u8"STEP_FAILED")) != u8str::npos ||
        result.find(u8str(u8"[bad_alloc")) != u8str::npos ||
        result.find(u8str(u8"[LLM Error]")) != u8str::npos ||
        result.find(u8str(u8"[PlanExecuteLoop Error")) != u8str::npos ||
        result.find(u8str(u8"[Tool Not Found]")) != u8str::npos ||
        result.find(u8str(u8"[MCP Error]")) != u8str::npos ||
        result.find(u8str(u8"[Tool Exception]")) != u8str::npos ||
        (result.find(u8str(u8"Error")) != u8str::npos &&
         result.find(u8str(u8"[PlanExec")) != u8str::npos)) {
        return StepValidation::Fail;
    }

    // 如果步骤有 expected_output，检查结果是否匹配
    if (step.expected_output) {
        std::string expected(step.expected_output->begin(), step.expected_output->end());
        std::string actual(result.begin(), result.end());
        // 简单的包含检查：如果 expected_output 在结果中找到，则通过
        if (!expected.empty() && actual.find(expected) == std::string::npos) {
            // expected_output 未在结果中找到，但不确定是否真的失败
            return StepValidation::Uncertain;
        }
    }

    return StepValidation::Pass;
}

bool PlanExecuteLoop::replan(const u8str& failed_step_id, const u8str& failure_reason) {
    if (replan_count_ >= config_.replan_config.max_replan_attempts) {
        PE_ERROR("Max replan attempts (" << config_.replan_config.max_replan_attempts << ") reached");
        return false;
    }

    ++replan_count_;
    current_plan_.replan_count = replan_count_;

    PE_DEBUG("Replanning (attempt " << replan_count_ << "/"
              << config_.replan_config.max_replan_attempts << ") due to step "
              << std::string(failed_step_id.begin(), failed_step_id.end())
              << " failure: " << std::string(failure_reason.begin(), failure_reason.end()));

    // 构建重规划 prompt
    u8str replan_prompt = build_replan_prompt(failed_step_id, failure_reason);

    // 添加到上下文
    context_.add_user_message(replan_prompt);

    // 调用 planner LLM
    u8str system_prompt;
    try {
        system_prompt = prompt_builder_.build_system_prompt(personality_docs_, plan_execute_instruction());
    } catch (const std::bad_alloc& e) {
        PE_ERROR("bad_alloc in replan build_system_prompt: " << e.what());
        return false;
    }

    LlmRequest request;
    request.messages = context_.get_messages();
    request.system_prompt = system_prompt;

    // 重规划阶段传递工具列表（与 Phase 1 一致），但忽略 tool_calls
    request.tools = cached_tools_schema_;

    LlmResponse response;
    try {
        response = call_llm_for_plan(request);
    } catch (const std::exception& e) {
        PE_ERROR("Replan LLM call failed: " << e.what());
        return false;
    }

    if (response.is_error) {
        PE_ERROR("Replan LLM error: " << std::string(response.error_message.begin(), response.error_message.end()));
        return false;
    }

    // 忽略重规划阶段的 tool_calls，只使用文本内容
    if (!response.tool_calls.empty()) {
        PE_DEBUG("Replan: LLM returned tool_calls, ignoring (plan-only phase)");
        response.tool_calls.clear();
    }

    // 记录 thinking step
    if (config_.base.enable_thinking) {
        ThinkingStep thinking_step;
        thinking_step.thinking_content = response.content;
        thinking_step.phase = u8str(u8"replanning");
        thinking_step.timestamp = std::chrono::system_clock::now();
        add_thinking_step(std::move(thinking_step));
    }

    // 解析新计划
    Plan new_plan;
    try {
        new_plan = parse_plan(response.content);
    } catch (const std::exception& e) {
        PE_ERROR("Replan parse failed: " << e.what());
        return false;
    }

    if (new_plan.steps.empty()) {
        PE_ERROR("Replan produced empty plan");
        return false;
    }

    context_.add_assistant_message(response.content);

    // 如果配置了保留已完成步骤，合并计划
    if (config_.replan_config.preserve_completed && plan_graph_) {
        // 保留已完成的步骤，替换未完成的步骤
        for (auto& new_step : new_plan.steps) {
            // 检查是否与已完成步骤冲突
            auto existing = plan_graph_->get_step(new_step.id);
            if (existing && (existing->status == u8str(u8"completed"))) {
                // 保留已完成步骤，不替换
                continue;
            }
            // 更新或添加新步骤
            bool found = false;
            for (auto& s : current_plan_.steps) {
                if (s.id == new_step.id) {
                    s = new_step;
                    s.status = u8str(u8"pending");
                    found = true;
                    break;
                }
            }
            if (!found) {
                new_step.status = u8str(u8"pending");
                current_plan_.steps.push_back(new_step);
            }
            if (plan_graph_) {
                plan_graph_->update_step(new_step);
                // 清除失败状态，允许重新执行
                plan_graph_->clear_failed(new_step.id);
            }
        }
    } else {
        // 不保留已完成步骤，完全替换
        current_plan_ = new_plan;
        plan_graph_ = std::make_unique<PlanGraph>(current_plan_);
    }

    PE_DEBUG("Replan complete, new plan has " << current_plan_.steps.size() << " steps");

    // 记录到执行日志
    if (execution_log_) {
        PlanStepLog log_entry;
        log_entry.step_id = failed_step_id;
        log_entry.replan_reason = failure_reason;
        execution_log_->step_logs.push_back(std::move(log_entry));
        execution_log_->replan_count = replan_count_;
    }

    return true;
}

u8str PlanExecuteLoop::build_replan_prompt(const u8str& failed_step_id, const u8str& failure_reason) const {
    u8str prompt = u8str(u8"The plan execution encountered a problem. Please create a revised plan.\n\n");

    // 当前计划状态
    prompt += u8str(u8"=== Current Plan Status ===\n");
    for (const auto& step : current_plan_.steps) {
        std::string id(step.id.begin(), step.id.end());
        std::string status(step.status.begin(), step.status.end());
        std::string desc(step.description.begin(), step.description.end());
        prompt += u8str(u8"Step ") + u8str(id.begin(), id.end())
            + u8str(u8" [") + u8str(status.begin(), status.end())
            + u8str(u8"]: ") + u8str(desc.begin(), desc.end());

        if (!step.result.empty()) {
            std::string result_snippet(step.result.begin(), step.result.end());
            if (result_snippet.length() > 100) {
                result_snippet = result_snippet.substr(0, 100) + "...";
            }
            prompt += u8str(u8"\n  Result: ") + u8str(result_snippet.begin(), result_snippet.end());
        }
        prompt += u8str(u8"\n");
    }

    // 失败信息
    prompt += u8str(u8"\n=== Failure Details ===\n");
    prompt += u8str(u8"Failed step: ") + failed_step_id + u8str(u8"\n");
    prompt += u8str(u8"Reason: ") + failure_reason + u8str(u8"\n");

    // 重规划指令
    prompt += u8str(u8"\n=== Instructions ===\n");
    prompt += u8str(u8"Please create a new plan that addresses the failure. ");
    if (config_.replan_config.preserve_completed) {
        prompt += u8str(u8"Keep the completed steps and only revise the remaining steps. ");
    }
    prompt += u8str(u8"Output the revised plan in the same format as before (JSON or text).\n");

    return prompt;
}

} // namespace agent