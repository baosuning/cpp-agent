#pragma once
#include "types.h"
#include "i_llm_provider.h"
#include "i_user_confirm_handler.h"
#include "i_prompt_builder.h"
#include "i_context_manager.h"
#include "i_agent_loop.h"
#include "i_tool.h"
#include "i_mcp.h"
#include "i_memory.h"
#include "personality.h"
#include <functional>
#include <memory>
#include <vector>
#include <filesystem>

namespace agent {

// 前向声明：TokenUsageAccumulator 是内部实现（定义在 src/agent/token_usage_accumulator.h）
// 公开头文件只持有指针，不需要完整定义
struct TokenUsageAccumulator;

// Agent 运行模式
enum class AgentMode {
    ReAct,           // Reasoning + Acting 交替循环（默认）
    PlanAndExecute,  // 先规划再逐步执行
    Reflection,      // Generate → Critique → Refine 循环，自我审视并改进输出质量
};

// 内置 Loop 配置
// 外部通过此结构体配置内置 Loop（ReAct、PlanAndExecute 等）的参数
struct InnerLoopConfig {
    int                            max_steps = 10;
    bool                           enable_thinking = true;
    bool                           auto_confirm = false;
    bool                           debug = false;
};

// 重规划策略配置
struct ReplanConfig {
    int  max_replan_attempts{3};     // 最大重规划次数
    bool replan_on_validation{true}; // 校验失败时是否重规划
    bool replan_on_failure{true};    // 步骤失败时是否重规划
    bool preserve_completed{true};   // 重规划时保留已完成步骤
};

// Agent 创建配置
// 所有组件使用智能指针传递，nullptr 表示使用内置默认实现
struct AgentConfig {
    // 模型配置（llm_provider 为 nullptr 时，内部据此自动创建内置的 llm_provider）
    LlmModelConfig                  model_config;
    // 自定义 llm_provider（nullptr 表示使用model_config创建内置的 llm_provider）
    LlmProviderPtr                  llm_provider;
    // 组件（nullptr 表示使用内置默认实现）
    UserConfirmHandlerPtr           confirm_handler;
    PromptBuilderPtr                prompt_builder;
    ContextManagerPtr               context_manager;
    ToolRegistryPtr                 tool_registry;
    MemoryPtr                       memory;

    // 人格配置
    PersonalityDocs                 personality;

    // Skill 目录路径（内部自动扫描并注册 ReadSkillTool）
    std::vector<std::filesystem::path> skill_dirs;

    // MCP 配置文件路径（内部自动加载并连接 MCP 服务器）
    std::filesystem::path              mcp_config_path;
};

// AgentLoop 工厂上下文，包含创建 Loop 所需的全部运行时依赖
struct AgentLoopContext {
    IContextManager&                context_manager;
    IPromptBuilder&                 prompt_builder;
    IToolRegistry&                  tools;
    IMcpManager&                    mcps;
    IMemory&                        memory;
    const PersonalityDocs&          personality;
    LlmProviderPtr                  llm_provider;
    UserConfirmHandlerPtr           confirm_handler;
    TokenUsageAccumulator*          token_accumulator = nullptr;  // 会话级 token 统计累加器
};

// 自定义 AgentLoop 工厂函数类型
using AgentLoopFactory = std::function<std::shared_ptr<IAgentLoop>(const AgentLoopContext&)>;

// ========== 模式专属配置 ==========

// ReAct 模式专属配置
struct ReactAgentConfig : AgentConfig {
    int     max_steps{15};
    bool    enable_thinking{true};
    bool    auto_confirm{false};
    bool    debug{false};
};

// Plan-and-Execute 模式专属配置
struct PlanExecuteAgentConfig : AgentConfig {
    // PE Loop 基础配置
    int     max_steps{15};
    bool    enable_thinking{true};
    bool    auto_confirm{false};
    bool    debug{false};

    // PE 特有：双模型配置
    LlmProviderPtr      planner_llm_provider;    // 规划用 LLM（nullptr 则使用 AgentConfig::llm_provider）
    LlmProviderPtr      executor_llm_provider;   // 执行用 LLM（nullptr 则使用 AgentConfig::llm_provider）
    LlmModelConfig      planner_model_config;    // 规划模型配置
    LlmModelConfig      executor_model_config;   // 执行模型配置

    // PE 特有：重规划配置
    ReplanConfig        replan_config;
    int                 max_step_retries{2};     // 单步最大重试次数
};

// Reflection 模式专属配置
struct ReflectionAgentConfig : AgentConfig {
    // Reflection Loop 基础配置
    int     max_steps{15};
    bool    enable_thinking{true};
    bool    auto_confirm{false};
    bool    debug{false};

    // Reflection 特有：反思轮次
    int     max_reflection_rounds{3};           // 最大 Critique → Refine 循环次数

    // Reflection 特有：双模型配置（Critic 可使用独立模型）
    LlmProviderPtr      critic_llm_provider;    // 批评用 LLM（nullptr 则使用 AgentConfig::llm_provider）
    LlmModelConfig      critic_model_config;    // 批评模型配置
};

// ReAct Loop 配置（内部使用，从 ReactAgentConfig 构造）
struct ReactLoopConfig {
    InnerLoopConfig base;

    ReactLoopConfig() = default;
    ReactLoopConfig(const ReactAgentConfig& config)
        : base{config.max_steps, config.enable_thinking, config.auto_confirm, config.debug} {}
    ReactLoopConfig(const InnerLoopConfig& config)
        : base(config) {}

    operator InnerLoopConfig() const { return base; }
};

// Plan-Execute Loop 配置（内部使用，从 PlanExecuteAgentConfig 构造）
struct PlanExecuteLoopConfig {
    InnerLoopConfig base;

    // PE 特有
    ReplanConfig    replan_config;
    int             max_step_retries{2};

    PlanExecuteLoopConfig() = default;
    PlanExecuteLoopConfig(const PlanExecuteAgentConfig& config)
        : base{config.max_steps, config.enable_thinking, config.auto_confirm, config.debug}
        , replan_config(config.replan_config)
        , max_step_retries(config.max_step_retries) {}
    PlanExecuteLoopConfig(const InnerLoopConfig& config)
        : base(config) {}

    operator InnerLoopConfig() const { return base; }
};

// Reflection Loop 配置（内部使用，从 ReflectionAgentConfig 构造）
struct ReflectionLoopConfig {
    InnerLoopConfig base;

    // Reflection 特有
    int             max_reflection_rounds{3};

    ReflectionLoopConfig() = default;
    ReflectionLoopConfig(const ReflectionAgentConfig& config)
        : base{config.max_steps, config.enable_thinking, config.auto_confirm, config.debug}
        , max_reflection_rounds(config.max_reflection_rounds) {}
    ReflectionLoopConfig(const InnerLoopConfig& config)
        : base(config) {}

    operator InnerLoopConfig() const { return base; }
};

} // namespace agent
