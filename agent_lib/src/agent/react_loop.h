#pragma once
#include "agent_loop_base.h"
#include <agent/i_llm_provider.h>
#include <agent/i_user_confirm_handler.h>
#include <agent/i_tool.h>
#include <agent/i_prompt_builder.h>
#include <agent/agent_config.h>
#include <functional>
#include <atomic>
#include <mutex>
#include <vector>
#include <optional>
#include <queue>

namespace agent {

class IContextManager;
class IToolRegistry;
class IMcpManager;
class IMemory;
struct PersonalityDocs;

class ReactLoop : public AgentLoopBase {
public:
    ReactLoop(LlmProviderPtr llm_provider,
              UserConfirmHandlerPtr confirm_handler,
              IContextManager& context,
              IPromptBuilder& prompt_builder,
              IToolRegistry& tools,
              IMcpManager& mcps,
              IMemory& memory,
              const PersonalityDocs& personality,
              ReactLoopConfig config = {},
              TokenUsageAccumulator* token_accumulator = nullptr);

    ~ReactLoop() override;

    void run(const u8str& user_input) override;
    void interrupt(const u8str& new_input) override;
    void stop() override;
    std::optional<Plan> get_plan() const override;
    bool should_auto_continue() const override;
    bool needs_user_input() const override;

private:
    ToolResult     execute_tool(const ToolCall& tool_call);
    bool           needs_confirmation(const ToolCall& tool_call) const;
    u8str          react_instruction() const;

    // 最后一次 LLM 响应是否包含 tool_calls
    // 用于区分"自然完成"（纯文本回答）和"被迫中断"（max_steps 耗尽）
    bool           last_response_had_tool_calls_{false};
};

} // namespace agent
