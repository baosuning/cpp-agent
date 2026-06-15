#pragma once
#include "types.h"
#include <memory>

namespace agent {

class IContextManager {
public:
    virtual ~IContextManager() = default;

    virtual void add_message(Message message) = 0;
    virtual void add_user_message(const u8str& content) = 0;
    virtual void add_assistant_message(const u8str& content) = 0;
    virtual void add_assistant_message(const u8str& content, std::vector<ToolCall> tool_calls) = 0;
    virtual void add_assistant_message(const u8str& content, std::vector<ToolCall> tool_calls, std::optional<u8str> reasoning_content) = 0;
    virtual void add_tool_message(const u8str& tool_call_id, const u8str& content) = 0;
    virtual void add_system_message(const u8str& content) = 0;

    virtual std::vector<Message> get_messages() const = 0;
    virtual ContextSnapshot get_snapshot(AgentState state,
                                         const std::vector<ThinkingStep>& thinking_steps,
                                         const std::optional<u8str>& final_output) const = 0;

    virtual void truncate_to_messages(size_t count) = 0;
    virtual void clear() = 0;
    virtual size_t message_count() const = 0;

    virtual void compress() = 0;
};

using ContextManagerPtr = std::shared_ptr<IContextManager>;

} // namespace agent