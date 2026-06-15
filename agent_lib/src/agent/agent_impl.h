#pragma once
#include <agent/agent.h>
#include <agent/agent_config.h>
#include <agent/i_agent_loop.h>
#include <agent/i_tool.h>
#include <agent/personality.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace agent {

class Agent::Impl {
public:
    Impl(const ReactAgentConfig& config);
    Impl(const PlanExecuteAgentConfig& config);
    Impl(const ReflectionAgentConfig& config);
    Impl(AgentLoopFactory loop_factory, const AgentConfig& config);

    ~Impl();

    void start();
    void stop();
    void submit_input(const u8str& input);

    std::vector<ThinkingStep> get_thinking() const;
    std::optional<u8str>      get_output() const;
    ContextSnapshot           get_context() const;
    AgentState                get_state() const;

    // 模式相关查询
    std::optional<Plan>              get_plan() const;
    std::optional<PlanExecutionLog>  get_execution_log() const;

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
    void process_loop();
    void init_components(const AgentConfig& config);
    void cleanup_browser_sessions();

    ContextManagerPtr                   context_;
    PromptBuilderPtr                    prompt_builder_;
    ToolRegistryPtr                     tools_;
    McpManagerPtr                       mcps_;
    MemoryPtr                           memory_;
    PersonalityDocs                     personality_docs_;

    LlmProviderPtr                      llm_provider_;
    UserConfirmHandlerPtr               confirm_handler_;
    AgentLoopFactory                    agent_loop_factory_;

    // PE 模式特有组件（仅 PlanExecute 构造函数填充）
    LlmProviderPtr                      planner_llm_;
    LlmProviderPtr                      executor_llm_;

    // Reflection 模式特有组件（仅 Reflection 构造函数填充）
    LlmProviderPtr                      critic_llm_;

    std::shared_ptr<IAgentLoop>         current_loop_;

    std::atomic<bool>                   running_{false};
    std::queue<u8str>                   input_queue_;
    mutable std::mutex                  queue_mutex_;
    std::condition_variable             queue_cv_;
    std::thread                         worker_thread_;

    std::function<void(const ThinkingStep&)>   on_thinking_update_;
    std::function<void(const u8str&)>          on_output_ready_;
    std::function<void(AgentState)>            on_state_change_;
    mutable std::mutex                         callback_mutex_;
};

} // namespace agent
