#include "react_loop.h"
#include <agent/i_context_manager.h>
#include <agent/i_tool.h>
#include <agent/i_mcp.h>
#include <agent/i_memory.h>
#include <agent/personality.h>
#include <util/u8str_utils.h>
#include "../util/utf8_utils.h"
#include <chrono>
#include <algorithm>
#include <cctype>
#include <future>
#include <nlohmann/json.hpp>

namespace agent {

ReactLoop::ReactLoop(LlmProviderPtr llm_provider,
                     UserConfirmHandlerPtr confirm_handler,
                     IContextManager& context,
                     IPromptBuilder& prompt_builder,
                     IToolRegistry& tools,
                     IMcpManager& mcps,
                     IMemory& memory,
                     const PersonalityDocs& personality,
                     ReactLoopConfig config,
                     TokenUsageAccumulator* token_accumulator)
    : AgentLoopBase(std::move(llm_provider),
      std::move(confirm_handler),
      context, prompt_builder, tools, mcps, memory,
      personality, static_cast<InnerLoopConfig>(config), token_accumulator) {}

ReactLoop::~ReactLoop() = default;

void ReactLoop::run(const u8str& user_input) {
    try {
        reset_loop_state();
        last_response_had_tool_calls_ = false;
        set_state(AgentState::Thinking);

        context_.add_user_message(prompt_builder_.build_user_prompt(user_input, personality_docs_));

        auto relevant_memories = memory_.search(user_input);
        if (!relevant_memories.empty()) {
            u8str memory_context;
            for (const auto& mem : relevant_memories) {
                memory_context += mem + u8str(u8"\n");
            }
            context_.add_system_message(memory_context);
        }

        u8str system_prompt = prompt_builder_.build_system_prompt(
            personality_docs_, react_instruction());

        // 预计算 tools_schema（在整个 run() 中不变）
        nlohmann::json cached_tools_schema;
        try {
            cached_tools_schema = build_combined_tools_schema();
        } catch (const std::exception& e) {
            AGENT_LOG_ERROR("ReAct") << "get_tools_schema error: " << e.what();
            emit_error(u8str_util::to_u8str(std::string("[get_tools_schema Error] ") + e.what()));
            return;
        }

        u8str last_content;

        for (int step = 0; step < config_.max_steps; ++step) {
            try {
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
                    }
                    continue;
                }

                if (stopped_.load()) {
                    set_state(AgentState::Idle);
                    return;
                }

                LlmRequest request;
                request.messages = context_.get_messages();
                request.system_prompt = system_prompt;
                request.tools = cached_tools_schema;

                if (config_.debug) {
                    AGENT_LOG_DEBUG("ReAct") << "Step " << (step + 1) << "/" << config_.max_steps
                                            << ": calling LLM with " << request.tools.size() << " tool(s)";
                }

                LlmResponse response;
                try {
                    response = llm_provider_->send_request(request);
                } catch (const std::exception& e) {
                    AGENT_LOG_ERROR("ReAct") << "LLM call error: " << e.what();
                    emit_error(u8str_util::to_u8str(std::string("[LLM Call Error] step=") + std::to_string(step)));
                    return;
                }

                record_token_usage(response);

                if (response.is_error) {
                    AGENT_LOG_ERROR("ReAct") << "LLM error: " << u8str_util::to_string(response.error_message);
                    u8str err_content = u8str(u8"[LLM Error] ") + response.error_message;
                    emit_error(err_content);
                    return;
                }

                if (!response.content.empty()) {
                    last_content = response.content;
                }

                {
                    std::string snippet = u8str_util::to_string(response.content);
                    if (snippet.length() > 100) snippet = snippet.substr(0, 100) + "...";
                    AGENT_LOG_DEBUG("ReAct") << "LLM response: tool_calls=" << response.tool_calls.size()
                                            << ", content=\"" << snippet << "\"";
                }

                if (config_.enable_thinking) {
                    ThinkingStep thinking_step;
                    if (response.thinking_content) {
                        thinking_step.thinking_content = std::move(*response.thinking_content);
                    } else {
                        thinking_step.thinking_content = response.content;
                    }
                    thinking_step.timestamp = std::chrono::system_clock::now();
                    add_thinking_step(std::move(thinking_step));
                }

                if (!response.tool_calls.empty()) {
                    last_response_had_tool_calls_ = true;
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
                        // 串行执行：需要用户确认，或只有单个工具
                        for (const auto& tool_call : response.tool_calls) {
                            if (config_.debug) {
                                std::string tc_name = u8str_util::to_string(tool_call.name);
                                std::string tc_args = u8str_util::to_string(tool_call.arguments);
                                AGENT_LOG_DEBUG("ReAct") << "Tool call: " << tc_name << "(" << tc_args.substr(0, 200) << ")";
                            }

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
                            if (config_.debug) {
                                std::string tr_content = u8str_util::to_string(tool_result.content);
                                AGENT_LOG_DEBUG("ReAct") << "Tool result: is_error=" << tool_result.is_error
                                                        << ", content=\"" << tr_content.substr(0, 200) << "\"";
                            }
                            context_.add_tool_message(tool_result.tool_call_id, tool_result.content);

                            ThinkingStep tool_step;
                            tool_step.tool_call = tool_call;
                            tool_step.tool_result = tool_result;
                            tool_step.timestamp = std::chrono::system_clock::now();
                            add_thinking_step(std::move(tool_step));
                        }
                    } else {
                        // 并行执行：所有工具均为 auto_confirm，同时启动
                        if (config_.debug) {
                            AGENT_LOG_DEBUG("ReAct") << "Executing " << response.tool_calls.size() << " tools in parallel";
                        }
                        set_state(AgentState::WaitingToolResult);

                        std::vector<std::future<ToolResult>> futures;
                        futures.reserve(response.tool_calls.size());
                        for (const auto& tc : response.tool_calls) {
                            futures.push_back(std::async(std::launch::async, [this, &tc]() {
                                return execute_tool(tc);
                            }));
                        }

                        set_state(AgentState::Thinking);

                        for (size_t i = 0; i < response.tool_calls.size(); ++i) {
                            const auto& tool_call = response.tool_calls[i];
                            ToolResult tool_result = futures[i].get();

                            if (config_.debug) {
                                std::string tr_content = u8str_util::to_string(tool_result.content);
                                std::string tc_name = u8str_util::to_string(tool_call.name);
                                AGENT_LOG_DEBUG("ReAct") << "Tool result [" << tc_name << "]: is_error=" << tool_result.is_error
                                                        << ", content=\"" << tr_content.substr(0, 200) << "\"";
                            }
                            context_.add_tool_message(tool_result.tool_call_id, tool_result.content);

                            ThinkingStep tool_step;
                            tool_step.tool_call = tool_call;
                            tool_step.tool_result = tool_result;
                            tool_step.timestamp = std::chrono::system_clock::now();
                            add_thinking_step(std::move(tool_step));
                        }
                    }

                    continue;
                }

                context_.add_assistant_message(response.content);

                // 纯文本回答 = 自然完成，不需要自动继续
                last_response_had_tool_calls_ = false;

                // 先输出，再设置 Completed（确保主循环先看到输出）
                emit_output(response.content);
                set_state(AgentState::Completed);

                memory_.store(u8str(u8"last_interaction"), response.content);
                return;
            } catch (const std::exception& e) {
                AGENT_LOG_ERROR("ReAct") << "Step error: " << e.what();
                emit_error(u8str_util::to_u8str(std::string("[ReactLoop Step Error] step=") + std::to_string(step)));
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lock(output_mutex_);
            if (!last_content.empty()) {
                final_output_ = last_content;
            } else {
                final_output_ = u8str(u8"[No output generated]");
            }
        }
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (on_output_ready_) {
                on_output_ready_(final_output_.value_or(u8str()));
            }
        }

        set_state(AgentState::Completed);

        memory_.store(u8str(u8"last_interaction"), final_output_.value_or(u8str()));
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("ReAct") << "Unexpected error: " << e.what();
        emit_error(u8str_util::to_u8str(std::string("[ReactLoop Error] ") + e.what()));
    }
}

void ReactLoop::interrupt(const u8str& new_input) {
    // 设置 interrupted_ 标志 + push pending_inputs_
    // Loop 在 WaitingUserConfirm 状态时，下一轮迭代会消费 pending_inputs_ 立即处理
    interrupted_ = true;
    std::lock_guard<std::mutex> lock(input_mutex_);
    pending_inputs_.push(new_input);
}

void ReactLoop::stop() {
    stopped_ = true;
}

std::optional<Plan> ReactLoop::get_plan() const {
    return std::nullopt;
}

bool ReactLoop::should_auto_continue() const {
    // 如果最后一次 LLM 响应没有 tool_calls，说明是自然完成，不需要自动继续
    if (!last_response_had_tool_calls_) return false;
    auto output = get_final_output();
    if (!output) return false;
    return !u8str_util::needs_user_input(*output);
}

bool ReactLoop::needs_user_input() const {
    auto output = get_final_output();
    if (!output) return false;
    return u8str_util::needs_user_input(*output);
}

ToolResult ReactLoop::execute_tool(const ToolCall& tool_call) {
    ToolResult result;
    result.tool_call_id = tool_call.id;
    result.is_error = false;

    std::string tool_name_str(tool_call.name.begin(), tool_call.name.end());

    ToolPtr tool = tools_.get_tool(tool_call.name);
    if (tool) {
        try {
            u8str args = tool_call.arguments;
            std::string tn(tool_call.name.begin(), tool_call.name.end());
            if (u8str_util::is_browser_launch_tool(tn)) {
                args = u8str_util::override_headless_false(args, tn);
            }
            result.content = llm::sanitize_utf8(tool->execute(args));
        } catch (const std::exception& e) {
            result.is_error = true;
            result.content = u8str_util::to_u8str(std::string("[Tool Exception] ") + e.what());
            AGENT_LOG_ERROR("ReAct") << "Tool exception [" << tool_name_str << "]: " << e.what();
        }
        return result;
    }

    // MCP tool
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
            auto dot_sep = tool_name_str.find('.');
            if (dot_sep != std::string::npos) {
                server_name = tool_name_str.substr(0, dot_sep);
                mcp_tool_name = tool_name_str.substr(dot_sep + 1);
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
            result.is_error = false;
        } catch (const std::exception& e) {
            result.is_error = true;
            result.content = u8str_util::to_u8str(std::string("[MCP Error] ") + e.what());
            AGENT_LOG_ERROR("ReAct") << "MCP call failed: " << e.what();
        }
    } else {
        result.is_error = true;
        result.content = u8str(u8"[Tool Not Found] ") + tool_call.name;
        AGENT_LOG_ERROR("ReAct") << "Tool not found: " << tool_name_str;
    }

    return result;
}

bool ReactLoop::needs_confirmation(const ToolCall& tool_call) const {
    if (config_.auto_confirm) {
        return false;
    }

    ToolPtr tool = tools_.get_tool(tool_call.name);
    if (tool) {
        if (tool->requires_confirmation()) {
            return true;
        }
        // 命令执行工具根据命令内容判断
        if (u8str_util::is_dangerous_command(tool_call.arguments)) {
            return true;
        }
    }

    return false;
}

u8str ReactLoop::react_instruction() const {
    static const u8str kInstruction = u8str(u8"You are an AI agent that follows the ReAct (Reasoning + Acting) pattern.\n"
        "\n"
        "For each step:\n"
        "1. THINK: Reason about the current situation and decide what to do next.\n"
        "2. ACT: If you need to use a tool, respond with a tool_call.\n"
        "3. OBSERVE: Process the result of the tool execution.\n"
        "4. Repeat until you can provide a final answer.\n"
        "\n"
        "All available tools are provided via function calling.\n"
        "Each MCP sub-tool is available as a separate callable function.\n"
        "Call them directly by their full name (e.g. servername_toolname).\n"
        "Follow the tool's parameter schema exactly when making calls.\n"
        "\n"
        "When you have the final answer, respond with your answer directly.\n"
        "If an action requires user confirmation, state that clearly.\n");
    return kInstruction;
}

} // namespace agent
