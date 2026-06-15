#pragma once
#include <agent/i_context_manager.h>
#include <shared_mutex>
#include <vector>

namespace agent {

class DefaultContextManager : public IContextManager {
public:
    void add_message(Message message) override;
    void add_user_message(const u8str& content) override;
    void add_assistant_message(const u8str& content) override;
    void add_assistant_message(const u8str& content, std::vector<ToolCall> tool_calls) override;
    void add_assistant_message(const u8str& content, std::vector<ToolCall> tool_calls, std::optional<u8str> reasoning_content) override;
    void add_tool_message(const u8str& tool_call_id, const u8str& content) override;
    void add_system_message(const u8str& content) override;

    std::vector<Message> get_messages() const override;
    ContextSnapshot get_snapshot(AgentState state,
                                 const std::vector<ThinkingStep>& thinking_steps,
                                 const std::optional<u8str>& final_output) const override;

    void truncate_to_messages(size_t count) override;
    void clear() override;
    size_t message_count() const override;
    void compress() override;

    void set_max_messages(size_t max_messages) { max_messages_ = max_messages; }
    size_t get_max_messages() const { return max_messages_; }

private:
    mutable std::shared_mutex mutex_;
    std::vector<Message> messages_;
    size_t max_messages_ = 0;
};

} // namespace agent