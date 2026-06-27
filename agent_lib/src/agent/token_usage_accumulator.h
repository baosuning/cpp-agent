#pragma once
// agent_lib/src/agent/token_usage_accumulator.h
// Token 使用量累加器（内部实现，不暴露到公开 API）
// 含互斥锁保护跨线程读写，由 Agent::Impl 持有

#include <agent/types.h>
#include <mutex>

namespace agent {

// Token 使用量累加器（内部使用，含互斥锁保护跨线程读写）
// 通过指针传递给各 Loop，在每次 LLM 调用后累加；由 Agent::Impl 持有
struct TokenUsageAccumulator {
    TokenUsageStats        stats;
    mutable std::mutex     mutex;

    TokenUsageAccumulator() = default;

    // 不可拷贝/移动（含 mutex 成员）
    TokenUsageAccumulator(const TokenUsageAccumulator&) = delete;
    TokenUsageAccumulator& operator=(const TokenUsageAccumulator&) = delete;
    TokenUsageAccumulator(TokenUsageAccumulator&&) = delete;
    TokenUsageAccumulator& operator=(TokenUsageAccumulator&&) = delete;

    // 线程安全地获取当前累计快照
    TokenUsageStats snapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        return stats;
    }

    // 线程安全地累加一次用量
    void accumulate(const TokenUsage& usage) {
        std::lock_guard<std::mutex> lock(mutex);
        stats.total_prompt_tokens     += usage.prompt_tokens;
        stats.total_completion_tokens += usage.completion_tokens;
        stats.total_tokens            += usage.total_tokens;
        stats.llm_call_count          += 1;
    }

    // 线程安全地清零（用于重置会话统计）
    void reset() {
        std::lock_guard<std::mutex> lock(mutex);
        stats = TokenUsageStats{};
    }
};

}  // namespace agent
