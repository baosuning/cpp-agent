#pragma once
#include <agent/types.h>
#include <memory>
#include <functional>
#include <vector>
#include <optional>

namespace agent {

class IAgentLoop {
public:
    virtual ~IAgentLoop() = default;

    virtual void run(const u8str& user_input) = 0;
    virtual void interrupt(const u8str& new_input) = 0;
    virtual void stop() = 0;

    virtual AgentState get_state() const = 0;
    virtual std::vector<ThinkingStep> get_thinking_steps() const = 0;
    virtual std::optional<u8str> get_final_output() const = 0;
    // 是否需要用户输入（各 Loop 自行决定语义）
    // ReAct: 检查输出文本模式
    // PE: 检查 WaitingUserConfirm 状态或步骤暂停标志
    virtual bool needs_user_input() const { return false; }

    // 获取执行日志（仅 PE 有意义，ReAct 返回 nullopt）
    virtual std::optional<PlanExecutionLog> get_execution_log() const { return std::nullopt; }

    virtual std::optional<Plan> get_plan() const = 0;

    virtual void set_on_thinking_update(std::function<void(const ThinkingStep&)> callback) = 0;
    virtual void set_on_output_ready(std::function<void(const u8str&)> callback) = 0;
    virtual void set_on_state_change(std::function<void(AgentState)> callback) = 0;

    // Loop 完成后是否应该自动继续（由各 Loop 实现自行决定策略）
    virtual bool should_auto_continue() const { return false; }
};

using AgentLoopPtr = std::shared_ptr<IAgentLoop>;

} // namespace agent
