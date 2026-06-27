// agent_lib/src/agent/agent_loop_base.cpp
// AgentLoopBase 实现

#include "agent_loop_base.h"
#include <util/u8str_utils.h>
#include <nlohmann/json.hpp>

namespace agent {

AgentLoopBase::AgentLoopBase(LlmProviderPtr llm_provider,
                              UserConfirmHandlerPtr confirm_handler,
                              IContextManager& context,
                              IPromptBuilder& prompt_builder,
                              IToolRegistry& tools,
                              IMcpManager& mcps,
                              IMemory& memory,
                              const PersonalityDocs& personality,
                              InnerLoopConfig config,
                              TokenUsageAccumulator* token_accumulator)
    : llm_provider_(std::move(llm_provider))
    , confirm_handler_(std::move(confirm_handler))
    , context_(context)
    , prompt_builder_(prompt_builder)
    , tools_(tools)
    , mcps_(mcps)
    , memory_(memory)
    , personality_docs_(personality)
    , config_(std::move(config))
    , token_accumulator_(token_accumulator) {}

AgentState AgentLoopBase::get_state() const {
    return state_.load();
}

std::vector<ThinkingStep> AgentLoopBase::get_thinking_steps() const {
    std::lock_guard<std::mutex> lock(thinking_mutex_);
    return thinking_steps_;
}

std::optional<u8str> AgentLoopBase::get_final_output() const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    return final_output_;
}

void AgentLoopBase::set_on_thinking_update(std::function<void(const ThinkingStep&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_thinking_update_ = std::move(callback);
}

void AgentLoopBase::set_on_output_ready(std::function<void(const u8str&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_output_ready_ = std::move(callback);
}

void AgentLoopBase::set_on_state_change(std::function<void(AgentState)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_state_change_ = std::move(callback);
}

void AgentLoopBase::set_state(AgentState state) {
    state_ = state;
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (on_state_change_) {
        on_state_change_(state);
    }
}

void AgentLoopBase::add_thinking_step(ThinkingStep step) {
    ThinkingStep step_copy;
    {
        std::lock_guard<std::mutex> lock(thinking_mutex_);
        step.step_index = static_cast<int>(thinking_steps_.size());
        thinking_steps_.push_back(step);
        step_copy = thinking_steps_.back();
    }
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (on_thinking_update_) {
            on_thinking_update_(step_copy);
        }
    }
}

void AgentLoopBase::emit_output(const u8str& content) {
    {
        std::lock_guard<std::mutex> lock(output_mutex_);
        final_output_ = content;
    }
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (on_output_ready_) {
            on_output_ready_(content);
        }
    }
}

void AgentLoopBase::emit_error(const u8str& error_msg) {
    // 错误信息先输出，再设置 Error 状态（确保主循环在看到 Error 状态前已输出错误）
    emit_output(error_msg);
    set_state(AgentState::Error);
}

void AgentLoopBase::reset_loop_state() {
    {
        std::lock_guard<std::mutex> lock(output_mutex_);
        final_output_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(thinking_mutex_);
        thinking_steps_.clear();
    }
    interrupted_ = false;
    stopped_ = false;
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        while (!pending_inputs_.empty()) {
            pending_inputs_.pop();
        }
    }
}

void AgentLoopBase::record_token_usage(const LlmResponse& response) {
    if (!token_accumulator_ || !response.usage || response.is_error) return;
    token_accumulator_->accumulate(*response.usage);
}

nlohmann::json AgentLoopBase::build_combined_tools_schema() const {
    nlohmann::json tools_list = tools_.get_tools_schema();
    auto mcps = mcps_.list_mcps();
    for (const auto& mcp : mcps) {
        auto server_name = mcp->name();
        std::string server_str(server_name.begin(), server_name.end());
        auto tools = mcp->list_tools();
        for (const auto& tool : tools) {
            if (!tool.contains("name")) continue;
            std::string tool_name = tool["name"].get<std::string>();
            std::string flat_name = server_str + "_" + tool_name;
            nlohmann::json entry;
            entry["type"] = "function";
            entry["function"]["name"] = flat_name;
            if (tool.contains("description")) {
                entry["function"]["description"] = "[MCP:" + server_str + "] " + tool["description"].get<std::string>();
            } else {
                entry["function"]["description"] = "[MCP:" + server_str + "] " + tool_name;
            }
            if (tool.contains("inputSchema")) {
                entry["function"]["parameters"] = tool["inputSchema"];
            } else {
                nlohmann::json params;
                params["type"] = "object";
                params["properties"]["_dummy"]["type"] = "string";
                params["properties"]["_dummy"]["description"] = "Tool parameters";
                entry["function"]["parameters"] = params;
            }
            tools_list.push_back(std::move(entry));
        }
    }
    return tools_list;
}

}  // namespace agent
