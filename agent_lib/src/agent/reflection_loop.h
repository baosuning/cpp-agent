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
#include <optional>
#include <memory>

namespace agent {

class IContextManager;
class IToolRegistry;
class IMcpManager;
class IMemory;
struct PersonalityDocs;

// 批评者返回的结构化评价
struct CritiqueResult {
    int                score{0};          // 1-10
    std::vector<u8str> issues;           // 发现的问题
    std::vector<u8str> suggestions;      // 改进建议
    bool               acceptable{false}; // 是否接受当前回答
};

class ReflectionLoop : public AgentLoopBase {
public:
    ReflectionLoop(LlmProviderPtr llm_provider,
                   UserConfirmHandlerPtr confirm_handler,
                   IContextManager& context,
                   IPromptBuilder& prompt_builder,
                   IToolRegistry& tools,
                   IMcpManager& mcps,
                   IMemory& memory,
                   const PersonalityDocs& personality,
                   ReflectionLoopConfig config = {},
                   LlmProviderPtr critic_llm = nullptr);

    ~ReflectionLoop() override;

    void run(const u8str& user_input) override;
    void interrupt(const u8str& new_input) override;
    void stop() override;
    std::optional<Plan> get_plan() const override;
    bool should_auto_continue() const override;
    bool needs_user_input() const override;

private:
    // ========== Phase 1: 生成 ==========

    // 调用 LLM 生成回答（支持工具调用），返回最终文本内容
    // 内部实现一个简化的 ReAct 子循环：LLM 可以调用工具，直到给出纯文本回答或达到 max_steps
    u8str     generate_answer(const u8str& system_prompt,
                              const nlohmann::json& tools_schema);

    // ========== Phase 2: 反思与改进 ==========

    // 调用 Critic LLM 评价当前回答
    CritiqueResult critique(const u8str& user_query, const u8str& answer);

    // 调用 Generator LLM 根据批评反馈改进回答
    u8str     refine_answer(const u8str& system_prompt,
                             const u8str& user_query,
                             const u8str& current_answer,
                             const CritiqueResult& critique,
                             const nlohmann::json& tools_schema,
                             size_t phase1_message_count);

    // ========== 工具执行 ==========

    ToolResult execute_tool(const ToolCall& tool_call);
    bool       needs_confirmation(const ToolCall& tool_call) const;

    // ========== Prompt 指令 ==========

    u8str      reflection_instruction() const;
    u8str      critique_instruction(const u8str& user_query, const u8str& answer) const;
    u8str      self_critique_instruction(const u8str& user_query, const u8str& answer) const;
    u8str      refine_instruction(const u8str& user_query,
                                  const u8str& current_answer,
                                  const CritiqueResult& critique) const;

    // ========== 辅助方法 ==========

    // 解析 Critic 的结构化 JSON 评价
    CritiqueResult parse_critique_response(const u8str& response) const;

    // 序列化反思历史用于 Memory 存储
    u8str      serialize_reflection_history(int total_rounds,
                                            const std::vector<CritiqueResult>& history) const;

    // ========== 成员变量 ==========

    ReflectionLoopConfig      ref_config_;
    LlmProviderPtr            critic_llm_;            // 批评用 LLM（可与主 LLM 相同或不同）
};

} // namespace agent