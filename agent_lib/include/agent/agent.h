#pragma once
#include "types.h"
#include "agent_config.h"
#include "i_memory.h"
#include "i_tool.h"
#include "i_mcp.h"
#include <memory>
#include <functional>
#include <optional>

namespace agent {

class Agent;
using AgentPtr = std::shared_ptr<Agent>;

class Agent {
public:
    // 创建 ReAct 模式 Agent
    static AgentPtr create_react(const ReactAgentConfig& config);

    // 创建 Plan-and-Execute 模式 Agent
    static AgentPtr create_plan_execute(const PlanExecuteAgentConfig& config);

    // 创建 Reflection 模式 Agent（Generate → Critique → Refine 循环）
    static AgentPtr create_reflection(const ReflectionAgentConfig& config);

    // 创建自定义 Loop Agent（高级用法）
    static AgentPtr create_custom(AgentLoopFactory loop_factory, const AgentConfig& config);

    ~Agent();

    // 生命周期
    void start();
    void stop();

    // 输入
    void submit_input(const u8str& input);

    // 状态查询
    std::vector<ThinkingStep> get_thinking() const;
    std::optional<u8str>      get_output() const;
    ContextSnapshot           get_context() const;
    AgentState                get_state() const;

    // 模式相关查询
    std::optional<Plan>              get_plan() const;
    std::optional<PlanExecutionLog>  get_execution_log() const;

    // Token 使用统计（会话级累计，跨多轮对话）
    TokenUsageStats                  get_token_stats() const;
    void                             reset_token_stats();

    // 组件查询
    LlmProviderPtr              get_llm_provider() const;
    UserConfirmHandlerPtr       get_confirm_handler() const;
    PromptBuilderPtr            get_prompt_builder() const;
    ContextManagerPtr           get_context_manager() const;
    ToolRegistryPtr             get_tool_registry() const;
    McpManagerPtr               get_mcp_manager() const;
    MemoryPtr                   get_memory() const;
    const PersonalityDocs&      get_personality() const;

    // 回调
    void set_on_thinking_update(std::function<void(const ThinkingStep&)> callback);
    void set_on_output_ready(std::function<void(const u8str&)> callback);
    void set_on_state_change(std::function<void(AgentState)> callback);

private:
    Agent() = default;
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace agent
