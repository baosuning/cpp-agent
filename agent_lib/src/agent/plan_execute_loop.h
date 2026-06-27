#pragma once
#include "agent_loop_base.h"
#include "plan_graph.h"
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
#include <memory>

namespace agent {

class IContextManager;
class IToolRegistry;
class IMcpManager;
class IMemory;
struct PersonalityDocs;

class PlanExecuteLoop : public AgentLoopBase {
public:
    PlanExecuteLoop(LlmProviderPtr llm_provider,
                    UserConfirmHandlerPtr confirm_handler,
                    IContextManager& context,
                    IPromptBuilder& prompt_builder,
                    IToolRegistry& tools,
                    IMcpManager& mcps,
                    IMemory& memory,
                    const PersonalityDocs& personality,
                    PlanExecuteLoopConfig config = {},
                    LlmProviderPtr planner_llm = nullptr,
                    LlmProviderPtr executor_llm = nullptr,
                    TokenUsageAccumulator* token_accumulator = nullptr);

    ~PlanExecuteLoop() override;

    void run(const u8str& user_input) override;
    void interrupt(const u8str& new_input) override;
    void stop() override;
    std::optional<Plan> get_plan() const override;
    bool should_auto_continue() const override;
    bool needs_user_input() const override;
    std::optional<PlanExecutionLog> get_execution_log() const override;

private:
    ToolResult     execute_tool(const ToolCall& tool_call);
    bool           needs_confirmation(const ToolCall& tool_call) const;
    Plan           parse_plan(const u8str& llm_output) const;
    Plan           try_parse_json_plan(const u8str& llm_output) const;
    Plan           parse_text_plan(const u8str& llm_output) const;
    void           execute_single_step(const u8str& step_prompt, const u8str& system_prompt);
    u8str          plan_execute_instruction() const;
    u8str          execution_instruction() const;

    // 重规划
    bool           replan(const u8str& failed_step_id, const u8str& failure_reason);
    u8str          build_replan_prompt(const u8str& failed_step_id, const u8str& failure_reason) const;

    // 步骤结果校验
    StepValidation validate_step_result(const PlanStep& step, const u8str& result) const;

    // 步骤执行条件判断
    bool           should_execute_step(const PlanStep& step) const;

    // 规划/执行专用 LLM（nullptr 则用 llm_provider_）
    LlmResponse    call_llm_for_plan(const LlmRequest& request);
    LlmResponse    call_llm_for_execute(const LlmRequest& request);

    // 构建步骤执行 prompt（含 tool_hint / tool_args_hint）
    u8str          build_step_prompt(const PlanStep& step, int step_idx, int total) const;

    // 步骤结果处理动作
    enum class StepAction { Continue, Retry, ReplanFromStart, Pause, Break };

    // 统一处理步骤结果（校验、重试、重规划、进度输出、日志记录）
    StepAction     process_step_result(PlanStep& step, int step_idx, int total);

    // 输出步骤进度（完成/失败）
    void           emit_step_progress(const PlanStep& step, int total, bool is_success) const;

    // Phase 3: 汇总所有步骤结果，生成最终答案
    void           summarize_all_steps(u8str& final_output);

    // 设置最终输出并通知回调（封装 output_mutex_ + callback_mutex_ 锁对）
    void           set_final_output(const u8str& output);

    Plan                               current_plan_;
    int                                current_step_index_{-1};
    bool                               paused_for_user_input_{false};
    PlanExecuteLoopConfig              config_;
    LlmProviderPtr                     planner_llm_;
    LlmProviderPtr                     executor_llm_;
    std::unique_ptr<PlanGraph>         plan_graph_;
    int                                replan_count_{0};
    std::optional<PlanExecutionLog>    execution_log_;
    nlohmann::json                     cached_tools_schema_;   // run() 内缓存，避免重复构建
};

} // namespace agent
