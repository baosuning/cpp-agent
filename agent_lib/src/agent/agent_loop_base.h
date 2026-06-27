#pragma once
// agent_lib/src/agent/agent_loop_base.h
// AgentLoop 基类（内部）：为内置 Loop 提供共享状态/回调/锁/错误处理逻辑
//
// 设计目标：
//   1. 消除 lock_guard + 回调调用的重复模式
//   2. 统一错误处理：emit_error() 统一先输出后通知状态
//   3. 统一 interrupted_/stopped_/pending_inputs_ 状态
//   4. 提供 set_state / add_thinking_step 公共实现
//
// 子类需实现：
//   - run(user_input)：业务循环
//   - 业务特定的辅助方法（如 ReactLoop::call_llm、PlanExecuteLoop::parse_plan 等）

#include <agent/i_agent_loop.h>
#include <agent/i_llm_provider.h>
#include <agent/i_user_confirm_handler.h>
#include <agent/i_tool.h>
#include <agent/i_prompt_builder.h>
#include <agent/i_context_manager.h>
#include <agent/i_mcp.h>
#include <agent/i_memory.h>
#include <agent/personality.h>
#include <agent/types.h>
#include <util/u8str_utils.h>
#include <util/log.h>
#include <agent/agent_config.h>
#include "token_usage_accumulator.h"

#include <functional>
#include <atomic>
#include <mutex>
#include <optional>
#include <queue>
#include <chrono>
#include <nlohmann/json.hpp>

namespace agent {

class AgentLoopBase : public IAgentLoop {
public:
    AgentLoopBase(LlmProviderPtr llm_provider,
                  UserConfirmHandlerPtr confirm_handler,
                  IContextManager& context,
                  IPromptBuilder& prompt_builder,
                  IToolRegistry& tools,
                  IMcpManager& mcps,
                  IMemory& memory,
                  const PersonalityDocs& personality,
                  InnerLoopConfig config,
                  TokenUsageAccumulator* token_accumulator = nullptr);

    ~AgentLoopBase() override = default;

    // IAgentLoop 公共实现
    AgentState get_state() const override;
    std::vector<ThinkingStep> get_thinking_steps() const override;
    std::optional<u8str> get_final_output() const override;
    void set_on_thinking_update(std::function<void(const ThinkingStep&)> callback) override;
    void set_on_output_ready(std::function<void(const u8str&)> callback) override;
    void set_on_state_change(std::function<void(AgentState)> callback) override;

protected:
    // ========== 状态管理辅助方法 ==========

    // 设置状态并通知回调（线程安全）
    void set_state(AgentState state);

    // 添加思考步骤并通知回调
    void add_thinking_step(ThinkingStep step);

    // 输出内容（先设置 final_output_，再调用 on_output_ready_）
    // 必须在 set_state(Completed) 之前调用，确保主循环先看到输出
    void emit_output(const u8str& content);

    // 输出错误（先 emit_output 错误信息，再 set_state(Error)）
    void emit_error(const u8str& error_msg);

    // 重置循环状态（在 run() 开始时调用）
    void reset_loop_state();

    // 构建合并的 tools_schema（内置工具 + MCP 工具）
    // 提取为基类方法，消除 ReactLoop 和 PlanExecuteLoop 中的重复代码
    nlohmann::json build_combined_tools_schema() const;

    // 记录一次 LLM 调用的 token 用量到会话级累加器
    // 在各 Loop 的 send_request 返回后调用；无 usage 或出错时跳过
    void record_token_usage(const LlmResponse& response);

    // ========== 共享成员 ==========
    LlmProviderPtr                llm_provider_;
    UserConfirmHandlerPtr         confirm_handler_;
    IContextManager&              context_;
    IPromptBuilder&               prompt_builder_;
    IToolRegistry&                tools_;
    IMcpManager&                  mcps_;
    IMemory&                      memory_;
    const PersonalityDocs&        personality_docs_;
    InnerLoopConfig               config_;
    TokenUsageAccumulator*        token_accumulator_ = nullptr;  // 会话级 token 统计

    // 状态
    std::atomic<AgentState>       state_{AgentState::Idle};
    std::vector<ThinkingStep>     thinking_steps_;
    mutable std::mutex            thinking_mutex_;

    std::optional<u8str>          final_output_;
    mutable std::mutex            output_mutex_;

    std::atomic<bool>             interrupted_{false};
    std::atomic<bool>             stopped_{false};
    std::queue<u8str>             pending_inputs_;
    mutable std::mutex            input_mutex_;

    // 回调
    std::function<void(const ThinkingStep&)>  on_thinking_update_;
    std::function<void(const u8str&)>         on_output_ready_;
    std::function<void(AgentState)>           on_state_change_;
    mutable std::mutex                        callback_mutex_;
};

}  // namespace agent
