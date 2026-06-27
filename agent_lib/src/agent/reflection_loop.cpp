#include "reflection_loop.h"
#include <agent/i_context_manager.h>
#include <agent/i_tool.h>
#include <agent/i_mcp.h>
#include <agent/i_memory.h>
#include <agent/personality.h>
#include <util/u8str_utils.h>
#include "../util/utf8_utils.h"
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace agent {

// ========== 构造函数 / 析构函数 ==========

ReflectionLoop::ReflectionLoop(LlmProviderPtr llm_provider,
                               UserConfirmHandlerPtr confirm_handler,
                               IContextManager& context,
                               IPromptBuilder& prompt_builder,
                               IToolRegistry& tools,
                               IMcpManager& mcps,
                               IMemory& memory,
                               const PersonalityDocs& personality,
                               ReflectionLoopConfig config,
                               LlmProviderPtr critic_llm,
                               TokenUsageAccumulator* token_accumulator)
    : AgentLoopBase(std::move(llm_provider),
      std::move(confirm_handler),
      context, prompt_builder, tools, mcps, memory,
      personality, static_cast<InnerLoopConfig>(config), token_accumulator)
    , ref_config_(std::move(config))
    , critic_llm_(critic_llm ? std::move(critic_llm) : llm_provider_)
{
}

ReflectionLoop::~ReflectionLoop() = default;

// ========== 主循环 ==========

void ReflectionLoop::run(const u8str& user_input) {
    try {
        reset_loop_state();
        set_state(AgentState::Thinking);

        // 构建 user prompt 并添加到 context
        u8str user_prompt = prompt_builder_.build_user_prompt(user_input, personality_docs_);
        context_.add_user_message(user_prompt);

        // 搜索相关记忆
        auto relevant_memories = memory_.search(user_input);
        if (!relevant_memories.empty()) {
            u8str memory_context;
            for (const auto& mem : relevant_memories) {
                memory_context += mem + u8str_util::to_u8str("\n");
            }
            context_.add_system_message(memory_context);
        }

        // 构建生成器 system prompt
        u8str system_prompt = prompt_builder_.build_system_prompt(
            personality_docs_, reflection_instruction());

        // 预计算 tools_schema
        nlohmann::json tools_schema;
        try {
            tools_schema = build_combined_tools_schema();
        } catch (const std::exception& e) {
            AGENT_LOG_ERROR("Reflection") << "get_tools_schema error: " << e.what();
            emit_error(u8str_util::to_u8str(std::string("[get_tools_schema Error] ") + e.what()));
            return;
        }

        // ====== Phase 1: Generate ======
        {
            ThinkingStep step;
            step.thinking_content = u8str_util::to_u8str("[Reflection] Generating initial answer...\n");
            step.timestamp = std::chrono::system_clock::now();
            add_thinking_step(std::move(step));
        }

        if (stopped_.load()) { set_state(AgentState::Idle); return; }

        u8str current_answer = generate_answer(system_prompt, tools_schema);
        if (current_answer.empty()) {
            emit_error(u8str_util::to_u8str("Failed to generate initial answer."));
            return;
        }

        // 保存 Phase 1 结束时的消息数量，用于每轮 Refine 前截断 Context
        size_t phase1_message_count = context_.message_count();

        // ====== Phase 2: Critique → Refine Loop ======
        std::vector<CritiqueResult> reflection_history;
        u8str final_answer = current_answer;

        for (int round = 1; round <= ref_config_.max_reflection_rounds; ++round) {
            if (stopped_.load()) { set_state(AgentState::Idle); return; }

            // --- Critique ---
            {
                std::string label = "[Reflection] Round " + std::to_string(round)
                    + "/" + std::to_string(ref_config_.max_reflection_rounds)
                    + ": Critiquing...\n";
                ThinkingStep step;
                step.thinking_content = u8str_util::to_u8str(label);
                step.timestamp = std::chrono::system_clock::now();
                add_thinking_step(std::move(step));
            }

            if (config_.debug) {
                std::string debug_label = "=== Critique Round " + std::to_string(round)
                    + "/" + std::to_string(ref_config_.max_reflection_rounds) + " ===";
                AGENT_LOG_DEBUG("Reflection") << debug_label;
            }

            CritiqueResult cr = critique(user_prompt, current_answer);
            cr.score = std::max(0, std::min(10, cr.score)); // clamp 1-10

            std::string result_label = "[Reflection] Round " + std::to_string(round)
                + "/" + std::to_string(ref_config_.max_reflection_rounds)
                + ": Critique result - score=" + std::to_string(cr.score)
                + "/10, acceptable=" + (cr.acceptable ? "YES" : "NO") + "\n";
            ThinkingStep step;
            step.thinking_content = u8str_util::to_u8str(result_label);
            step.timestamp = std::chrono::system_clock::now();
            add_thinking_step(std::move(step));

            reflection_history.push_back(cr);

            if (cr.acceptable) {
                final_answer = current_answer;
                {
                    ThinkingStep accept_step;
                    accept_step.thinking_content = u8str_util::to_u8str("[Reflection] Answer accepted (score=")
                        + u8str_util::to_u8str(std::to_string(cr.score)) + u8str_util::to_u8str("/10)\n");
                    accept_step.timestamp = std::chrono::system_clock::now();
                    add_thinking_step(std::move(accept_step));
                }
                break;
            }

            // 如果已经是最后一轮，不再 refine
            if (round >= ref_config_.max_reflection_rounds) {
                final_answer = current_answer;
                {
                    ThinkingStep limit_step;
                    limit_step.thinking_content = u8str_util::to_u8str(
                        std::string("[Reflection] Max reflection rounds (")
                        + std::to_string(ref_config_.max_reflection_rounds)
                        + ") reached. Using current answer.\n");
                    limit_step.timestamp = std::chrono::system_clock::now();
                    add_thinking_step(std::move(limit_step));
                }
                break;
            }

            // --- Refine ---
            {
                std::string refine_label = "[Reflection] Round " + std::to_string(round)
                    + "/" + std::to_string(ref_config_.max_reflection_rounds)
                    + ": Refining answer...\n";
                ThinkingStep refine_step;
                refine_step.thinking_content = u8str_util::to_u8str(refine_label);
                refine_step.timestamp = std::chrono::system_clock::now();
                add_thinking_step(std::move(refine_step));
            }

            if (config_.debug) {
                AGENT_LOG_DEBUG("Reflection") << "=== Refine Round " << round << "/"
                    << ref_config_.max_reflection_rounds << " ===";
            }

            current_answer = refine_answer(system_prompt, user_prompt, current_answer, cr, tools_schema, phase1_message_count);
            if (current_answer.empty()) {
                // Refine 失败，回退到上一轮改进后的答案
                current_answer = final_answer;
                break;
            }
            // Refine 成功，更新基准答案
            final_answer = current_answer;
        }

        // ====== Phase 3: Output ======
        emit_output(final_answer);
        set_state(AgentState::Completed);

        // 存储反思历史到 Memory
        u8str history_str = serialize_reflection_history(
            static_cast<int>(reflection_history.size()), reflection_history);
        memory_.store(u8str_util::to_u8str("reflection_history"), history_str);
        memory_.store(u8str_util::to_u8str("reflection_last_interaction"), final_answer);

    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("Reflection") << "Unexpected error: " << e.what();
        emit_error(u8str_util::to_u8str(std::string("[Reflection Error] ") + e.what()));
    }
}

// ========== Phase 1: Generate ==========

u8str ReflectionLoop::generate_answer(const u8str& system_prompt,
                                      const nlohmann::json& tools_schema) {
    u8str last_content;

    // 简化的 ReAct 子循环：生成器可以使用工具，直到给出纯文本回答
    for (int step = 0; step < ref_config_.base.max_steps; ++step) {
        if (stopped_.load()) return u8str{};
        if (interrupted_.load()) {
            interrupted_ = false;
            std::lock_guard<std::mutex> lock(input_mutex_);
            if (!pending_inputs_.empty()) {
                pending_inputs_.pop();
            }
            continue;
        }

        LlmRequest request;
        request.messages = context_.get_messages();
        request.system_prompt = system_prompt;
        request.tools = tools_schema;

        LlmResponse response;
        try {
            response = llm_provider_->send_request(request);
        } catch (const std::exception& e) {
            AGENT_LOG_ERROR("Reflection") << "Generate LLM call error: " << e.what();
            return last_content; // 返回已有的内容
        }

        record_token_usage(response);

        if (response.is_error) {
            AGENT_LOG_ERROR("Reflection") << "Generate LLM error: "
                << u8str_util::to_string(response.error_message);
            return last_content;
        }

        if (!response.content.empty()) {
            last_content = response.content;
        }

        // 如果没有工具调用 → 纯文本回答，返回
        if (response.tool_calls.empty()) {
            context_.add_assistant_message(response.content);
            return response.content;
        }

        // 有工具调用 → 执行工具，继续循环
        context_.add_assistant_message(response.content, response.tool_calls, response.thinking_content);

        // 串行执行工具
        for (const auto& tc : response.tool_calls) {
            if (stopped_.load()) return last_content;

            if (needs_confirmation(tc)) {
                set_state(AgentState::WaitingUserConfirm);
                if (confirm_handler_) {
                    ConfirmRequest confirm_req;
                    confirm_req.action_description = tc.name;
                    confirm_req.details = tc.arguments;
                    ConfirmResult result = confirm_handler_->confirm(confirm_req);
                    if (!result.confirmed) {
                        set_state(AgentState::Thinking);
                        ToolResult denied_result;
                        denied_result.tool_call_id = tc.id;
                        denied_result.content = u8str_util::to_u8str("User denied");
                        denied_result.is_error = true;
                        context_.add_tool_message(denied_result.tool_call_id, denied_result.content);
                        continue;
                    }
                }
                set_state(AgentState::Thinking);
            }

            set_state(AgentState::WaitingToolResult);
            ToolResult tr = execute_tool(tc);
            set_state(AgentState::Thinking);
            context_.add_tool_message(tr.tool_call_id, tr.content);
        }
    }

    return last_content;
}

// ========== Phase 2: Critique ==========

CritiqueResult ReflectionLoop::critique(const u8str& user_query, const u8str& answer) {
    u8str prompt = critique_instruction(user_query, answer);

    LlmRequest request;
    request.system_prompt = prompt;
    Message user_msg;
    user_msg.role = MessageRole::User;
    user_msg.content = u8str_util::to_u8str("Please evaluate the answer above and provide your structured critique.");
    request.messages.push_back(user_msg);

    // 最多 2 次尝试（1 初始 + 1 重试）
    for (int attempt = 0; attempt < 2; ++attempt) {
        LlmResponse response;
        try {
            response = critic_llm_->send_request(request);
        } catch (const std::exception& e) {
            AGENT_LOG_ERROR("Reflection") << "Critique LLM call error (attempt "
                << (attempt + 1) << "/2): " << e.what();
            if (attempt == 0) continue;
            break; // 两次都失败，退出循环进入 self-critique 回退
        }

        record_token_usage(response);

        if (!response.is_error) {
            return parse_critique_response(response.content);
        }

        AGENT_LOG_ERROR("Reflection") << "Critique LLM error (attempt "
            << (attempt + 1) << "/2): " << u8str_util::to_string(response.error_message);
        if (attempt == 0) continue;
    }

    // Critic LLM 失败，回退到 Generator 自评
    AGENT_LOG_WARN("Reflection") << "Critic unavailable, falling back to Generator self-critique";

    u8str self_prompt = self_critique_instruction(user_query, answer);
    LlmRequest self_request;
    self_request.system_prompt = self_prompt;
    Message self_user_msg;
    self_user_msg.role = MessageRole::User;
    self_user_msg.content = u8str_util::to_u8str(
        "Please review your own response above and provide a structured evaluation.");
    self_request.messages.push_back(self_user_msg);

    try {
        LlmResponse self_response = llm_provider_->send_request(self_request);
        record_token_usage(self_response);
        if (!self_response.is_error) {
            AGENT_LOG_INFO("Reflection") << "Self-critique completed successfully";
            return parse_critique_response(self_response.content);
        }
        AGENT_LOG_ERROR("Reflection") << "Self-critique LLM error: "
            << u8str_util::to_string(self_response.error_message);
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("Reflection") << "Self-critique LLM call error: " << e.what();
    }

    // 最终降级：Critic 和 Self-Critique 都失败
    AGENT_LOG_WARN("Reflection") << "Both Critic and Self-Critique failed, accepting answer";
    CritiqueResult fallback;
    fallback.acceptable = true;
    fallback.score = 5;
    return fallback;
}

// ========== Phase 2: Refine ==========

u8str ReflectionLoop::refine_answer(const u8str& system_prompt,
                                    const u8str& user_query,
                                    const u8str& current_answer,
                                    const CritiqueResult& critique,
                                    const nlohmann::json& tools_schema,
                                    size_t phase1_message_count) {
    // 回退 Context 到 Phase 1 结束时的状态，移除之前轮次的 Refine 消息
    context_.truncate_to_messages(phase1_message_count);

    // 构建改进指令作为新的 user message 添加到 context
    u8str refine_prompt = refine_instruction(user_query, current_answer, critique);
    context_.add_user_message(refine_prompt);

    // 调用 generate_answer 进行改进
    return generate_answer(system_prompt, tools_schema);
}

// ========== 生命周期 ==========

void ReflectionLoop::interrupt(const u8str& new_input) {
    interrupted_ = true;
    std::lock_guard<std::mutex> lock(input_mutex_);
    pending_inputs_.push(new_input);
}

void ReflectionLoop::stop() {
    stopped_ = true;
}

std::optional<Plan> ReflectionLoop::get_plan() const {
    return std::nullopt;
}

bool ReflectionLoop::should_auto_continue() const {
    return false; // Reflection 模式不需要自动继续
}

bool ReflectionLoop::needs_user_input() const {
    auto output = get_final_output();
    if (!output) return false;
    return u8str_util::needs_user_input(*output);
}

// ========== 工具执行 ==========

ToolResult ReflectionLoop::execute_tool(const ToolCall& tool_call) {
    ToolResult result;
    result.tool_call_id = tool_call.id;
    result.is_error = false;

    std::string tool_name_str(tool_call.name.begin(), tool_call.name.end());

    ToolPtr tool = tools_.get_tool(tool_call.name);
    if (tool) {
        try {
            u8str args = tool_call.arguments;
            if (u8str_util::is_browser_launch_tool(tool_name_str)) {
                args = u8str_util::override_headless_false(args, tool_name_str);
            }
            result.content = llm::sanitize_utf8(tool->execute(args));
        } catch (const std::exception& e) {
            result.is_error = true;
            result.content = u8str_util::to_u8str(std::string("[Tool Exception] ") + e.what());
            AGENT_LOG_ERROR("Reflection") << "Tool exception: " << e.what();
        }
        return result;
    }

    // MCP tool name resolution
    std::string server_name;
    std::string mcp_tool_name;
    McpPtr       resolved_mcp;

    if (tool_name_str.find("mcp__") == 0) {
        auto rest = tool_name_str.substr(5);
        auto sep = rest.find("__");
        if (sep != std::string::npos) {
            server_name = rest.substr(0, sep);
            mcp_tool_name = rest.substr(sep + 2);
            u8str su(server_name.begin(), server_name.end());
            resolved_mcp = mcps_.get_mcp(su);
        }
    } else {
        // 尝试下划线格式: {server}_{tool}
        auto sep_pos = tool_name_str.find('_');
        if (sep_pos != std::string::npos) {
            server_name = tool_name_str.substr(0, sep_pos);
            mcp_tool_name = tool_name_str.substr(sep_pos + 1);
            u8str su(server_name.begin(), server_name.end());
            resolved_mcp = mcps_.get_mcp(su);
        }
        // 尝试点号格式: {server}.{tool}
        if (!resolved_mcp) {
            sep_pos = tool_name_str.find('.');
            if (sep_pos != std::string::npos) {
                server_name = tool_name_str.substr(0, sep_pos);
                mcp_tool_name = tool_name_str.substr(sep_pos + 1);
                u8str su(server_name.begin(), server_name.end());
                resolved_mcp = mcps_.get_mcp(su);
            }
        }
        // 裸工具名 → 搜索所有 MCP 服务
        if (!resolved_mcp) {
            auto all_mcps = mcps_.list_mcps();
            for (const auto& mcp : all_mcps) {
                auto sname = mcp->name();
                std::string sn(sname.begin(), sname.end());
                auto tools = mcp->list_tools();
                for (const auto& t : tools) {
                    if (t.contains("name") && t["name"].get<std::string>() == tool_name_str) {
                        server_name = sn;
                        mcp_tool_name = tool_name_str;
                        resolved_mcp = mcp;
                        break;
                    }
                }
                if (resolved_mcp) break;
            }
        }
    }

    if (resolved_mcp) {
        try {
            u8str args = tool_call.arguments;
            if (u8str_util::is_browser_launch_tool(mcp_tool_name)) {
                args = u8str_util::override_headless_false(args, mcp_tool_name);
            }
            u8str method_u8(mcp_tool_name.begin(), mcp_tool_name.end());
            result.content = llm::sanitize_utf8(resolved_mcp->call(method_u8, args));
        } catch (const std::exception& e) {
            result.is_error = true;
            result.content = u8str_util::to_u8str(std::string("[MCP Error] ") + e.what());
            AGENT_LOG_ERROR("Reflection") << "MCP tool exception: " << e.what();
        }
    } else {
        result.is_error = true;
        result.content = u8str_util::to_u8str("[Tool Not Found] " + tool_name_str);
        AGENT_LOG_ERROR("Reflection") << "Tool not found: " << tool_name_str;
    }

    return result;
}

bool ReflectionLoop::needs_confirmation(const ToolCall& tool_call) const {
    if (config_.auto_confirm) return false;

    std::string name_str(tool_call.name.begin(), tool_call.name.end());
    ToolPtr tool = tools_.get_tool(tool_call.name);
    if (tool) {
        if (tool->requires_confirmation()) {
            return true;
        }
        // 命令执行工具根据命令内容判断是否为危险操作
        if (u8str_util::is_dangerous_command(tool_call.arguments)) {
            return true;
        }
    }
    return false;
}

// ========== Prompt 指令 ==========

u8str ReflectionLoop::reflection_instruction() const {
    static const u8str cached = u8str_util::to_u8str(
        "You are an AI agent that uses the Reflection pattern to produce high-quality responses.\n"
        "\n"
        "Your process works in phases:\n"
        "1. GENERATE: Produce an initial answer to the user's request. Use available tools "
        "if you need external information or to perform actions.\n"
        "2. CRITIQUE: Your answer will be evaluated by a critic. The critic checks for "
        "correctness, completeness, clarity, and actionability.\n"
        "3. REFINE: If the critic finds issues, you will receive specific feedback and "
        "have the opportunity to improve your answer.\n"
        "\n"
        "Guidelines:\n"
        "- Be thorough and accurate in your initial response.\n"
        "- Use tools when necessary to gather information or verify facts.\n"
        "- When given critique feedback, address ALL issues mentioned.\n"
        "- Think carefully about the user's intent and provide actionable responses.\n"
        "\n"
        "All available tools are provided via function calling.\n"
        "Each MCP sub-tool is available as a separate callable function.\n"
        "Call them directly by their full name (e.g. servername_toolname).\n"
        "Follow the tool's parameter schema exactly when making calls.\n"
        "\n"
        "When you have the final answer, respond with your answer directly."
    );
    return cached;
}

u8str ReflectionLoop::critique_instruction(const u8str& user_query, const u8str& answer) const {
    std::string prompt =
        "You are a meticulous reviewer. Your job is to critically evaluate the "
        "following output and provide constructive feedback.\n"
        "\n"
        "Evaluate based on these criteria:\n"
        "1. Correctness: Are there any factual errors or logical flaws?\n"
        "2. Completeness: Does it fully answer the user's question?\n"
        "3. Clarity: Is the response clear, well-structured, and easy to understand?\n"
        "4. Actionability: Can the user act on this information?\n"
        "\n"
        "User's original request:\n"
        "---\n"
        + u8str_util::to_string(user_query) + "\n"
        "---\n"
        "\n"
        "Output to evaluate:\n"
        "---\n"
        + u8str_util::to_string(answer) + "\n"
        "---\n"
        "\n"
        "Provide your evaluation as a JSON object with the following fields:\n"
        "{\n"
        "  \"score\": <integer 1-10>,\n"
        "  \"issues\": [<string list of specific problems found, empty if none>],\n"
        "  \"suggestions\": [<string list of concrete improvement suggestions, empty if none>],\n"
        "  \"acceptable\": <\"YES\" or \"NO\">\n"
        "}\n"
        "\n"
        "Score 8-10 means the answer is high quality and can be accepted.\n"
        "Score 5-7 means minor improvements needed.\n"
        "Score 1-4 means significant issues that must be fixed.\n"
        "Set acceptable to \"NO\" if score < 8, or if there are any factual errors.\n"
        "\n"
        "Output ONLY the JSON object, nothing else.";

    return u8str_util::to_u8str(prompt);
}

u8str ReflectionLoop::self_critique_instruction(const u8str& user_query, const u8str& answer) const {
    std::string prompt =
        "You are performing a self-review of your own previous response. "
        "Evaluate it critically and honestly, looking for any issues or areas "
        "for improvement.\n"
        "\n"
        "Check for:\n"
        "1. Correctness: Are there any factual errors or logical flaws?\n"
        "2. Completeness: Does it fully answer the user's question?\n"
        "3. Clarity: Is the response clear, well-structured, and easy to understand?\n"
        "4. Actionability: Can the user act on this information?\n"
        "\n"
        "User's original request:\n"
        "---\n"
        + u8str_util::to_string(user_query) + "\n"
        "---\n"
        "\n"
        "Your response to evaluate:\n"
        "---\n"
        + u8str_util::to_string(answer) + "\n"
        "---\n"
        "\n"
        "Provide your evaluation as a JSON object with the following fields:\n"
        "{\n"
        "  \"score\": <integer 1-10>,\n"
        "  \"issues\": [<string list of specific problems found, empty if none>],\n"
        "  \"suggestions\": [<string list of concrete improvement suggestions, empty if none>],\n"
        "  \"acceptable\": <\"YES\" or \"NO\">\n"
        "}\n"
        "\n"
        "Output ONLY the JSON object, nothing else.";

    return u8str_util::to_u8str(prompt);
}

u8str ReflectionLoop::refine_instruction(const u8str& user_query,
                                         const u8str& current_answer,
                                         const CritiqueResult& critique) const {
    std::string issues_str;
    for (const auto& issue : critique.issues) {
        issues_str += "- " + u8str_util::to_string(issue) + "\n";
    }
    if (issues_str.empty()) issues_str = "(none)";

    std::string suggestions_str;
    for (const auto& sug : critique.suggestions) {
        suggestions_str += "- " + u8str_util::to_string(sug) + "\n";
    }
    if (suggestions_str.empty()) suggestions_str = "(none)";

    std::string prompt =
        "You previously generated a response to the user's request, but the reviewer "
        "identified issues that need to be fixed. Please revise your response to address "
        "ALL the feedback.\n"
        "\n"
        "User's original request:\n"
        "---\n"
        + u8str_util::to_string(user_query) + "\n"
        "---\n"
        "\n"
        "Your previous response:\n"
        "---\n"
        + u8str_util::to_string(current_answer) + "\n"
        "---\n"
        "\n"
        "Reviewer feedback (score: " + std::to_string(critique.score) + "/10):\n"
        "\n"
        "Issues found:\n" + issues_str + "\n"
        "Improvement suggestions:\n" + suggestions_str + "\n"
        "\n"
        "Please generate an improved version that addresses ALL the issues mentioned above. "
        "You may use available tools to verify facts, gather additional information, or "
        "perform actions as needed.\n"
        "\n"
        "Provide your revised response directly.";

    return u8str_util::to_u8str(prompt);
}

// ========== 辅助方法 ==========

CritiqueResult ReflectionLoop::parse_critique_response(const u8str& response) const {
    CritiqueResult result;

    std::string content = u8str_util::to_string(response);

    std::string json_str;

    // 优先提取 ```json ... ``` 代码块中的 JSON
    auto block_start = content.find("```json");
    if (block_start != std::string::npos) {
        block_start = content.find('\n', block_start);
        if (block_start != std::string::npos) {
            ++block_start; // 跳过换行
            auto block_end = content.find("```", block_start);
            if (block_end != std::string::npos) {
                json_str = content.substr(block_start, block_end - block_start);
            }
        }
    }

    // 如果没有代码块，尝试直接提取 { ... }
    if (json_str.empty()) {
        auto json_start = content.find('{');
        auto json_end = content.rfind('}');
        if (json_start != std::string::npos && json_end != std::string::npos && json_end > json_start) {
            json_str = content.substr(json_start, json_end - json_start + 1);
        }
    }

    if (json_str.empty()) {
        AGENT_LOG_ERROR("Reflection") << "Failed to extract critique JSON, accepting answer";
        result.acceptable = true;
        result.score = 5;
        return result;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(json_str);

        if (j.contains("score") && j["score"].is_number()) {
            result.score = j["score"].get<int>();
        }
        if (j.contains("issues") && j["issues"].is_array()) {
            for (const auto& issue : j["issues"]) {
                result.issues.push_back(u8str_util::to_u8str(issue.get<std::string>()));
            }
        }
        if (j.contains("suggestions") && j["suggestions"].is_array()) {
            for (const auto& sug : j["suggestions"]) {
                result.suggestions.push_back(u8str_util::to_u8str(sug.get<std::string>()));
            }
        }
        if (j.contains("acceptable")) {
            if (j["acceptable"].is_boolean()) {
                result.acceptable = j["acceptable"].get<bool>();
            } else {
                std::string acc = j["acceptable"].get<std::string>();
                result.acceptable = (acc == "YES" || acc == "yes" || acc == "Yes" || acc == "true");
            }
        }
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("Reflection") << "JSON parse error in critique: " << e.what()
            << ". Raw: " << json_str.substr(0, 200);
        result.acceptable = true;
        result.score = 5;
    }

    return result;
}

u8str ReflectionLoop::serialize_reflection_history(
    int total_rounds,
    const std::vector<CritiqueResult>& history) const {
    nlohmann::json j;
    j["total_rounds"] = total_rounds;
    j["rounds"] = nlohmann::json::array();

    for (const auto& cr : history) {
        nlohmann::json round;
        round["score"] = cr.score;
        round["acceptable"] = cr.acceptable;
        nlohmann::json issues_arr = nlohmann::json::array();
        for (const auto& issue : cr.issues) {
            issues_arr.push_back(u8str_util::to_string(issue));
        }
        round["issues"] = issues_arr;
        nlohmann::json sug_arr = nlohmann::json::array();
        for (const auto& sug : cr.suggestions) {
            sug_arr.push_back(u8str_util::to_string(sug));
        }
        round["suggestions"] = sug_arr;
        j["rounds"].push_back(round);
    }

    return u8str_util::to_u8str(j.dump());
}

} // namespace agent