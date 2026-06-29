#include "context_manager.h"

namespace agent {

void DefaultContextManager::add_message(Message message) {
    std::unique_lock lock(mutex_);
    messages_.push_back(std::move(message));
}

void DefaultContextManager::add_user_message(const u8str& content) {
    Message msg;
    msg.role = MessageRole::User;
    msg.content = content;
    add_message(std::move(msg));
}

void DefaultContextManager::add_assistant_message(const u8str& content) {
    Message msg;
    msg.role = MessageRole::Assistant;
    msg.content = content;
    add_message(std::move(msg));
}

void DefaultContextManager::add_assistant_message(const u8str& content, std::vector<ToolCall> tool_calls) {
    Message msg;
    msg.role = MessageRole::Assistant;
    msg.content = content;
    msg.tool_calls = std::move(tool_calls);
    add_message(std::move(msg));
}

void DefaultContextManager::add_assistant_message(const u8str& content, std::vector<ToolCall> tool_calls, std::optional<u8str> reasoning_content) {
    Message msg;
    msg.role = MessageRole::Assistant;
    msg.content = content;
    msg.tool_calls = std::move(tool_calls);
    msg.reasoning_content = std::move(reasoning_content);
    add_message(std::move(msg));
}

void DefaultContextManager::add_tool_message(const u8str& tool_call_id, const u8str& content) {
    Message msg;
    msg.role = MessageRole::Tool;
    msg.content = content;
    msg.tool_call_id = tool_call_id;
    add_message(std::move(msg));
}

void DefaultContextManager::add_system_message(const u8str& content) {
    Message msg;
    msg.role = MessageRole::System;
    msg.content = content;
    add_message(std::move(msg));
}

std::vector<Message> DefaultContextManager::get_messages() const {
    std::shared_lock lock(mutex_);
    return messages_;
}

void DefaultContextManager::compress() {
    std::unique_lock lock(mutex_);
    if (max_messages_ == 0 || messages_.size() <= max_messages_) {
        return;
    }

    size_t keep_count = max_messages_ * 2 / 3;
    size_t compress_count = messages_.size() - keep_count;

    // 向前调整 compress_count，避免在 Tool 序列中间切断产生孤立 Tool 消息
    // （切断点落在 Tool 消息上时，把该 Tool 也纳入压缩范围）
    while (compress_count < messages_.size()
           && messages_[compress_count].role == MessageRole::Tool) {
        compress_count++;
    }

    u8str summary;
    for (size_t i = 0; i < compress_count; ++i) {
        const auto& msg = messages_[i];

        switch (msg.role) {
            case MessageRole::User:
                summary += u8str(u8"User: ") + msg.content + u8str(u8"\n");
                break;

            case MessageRole::Assistant:
                if (!msg.content.empty()) {
                    summary += u8str(u8"Assistant: ") + msg.content + u8str(u8"\n");
                }
                if (!msg.tool_calls.empty()) {
                    u8str tools_called;
                    for (size_t j = 0; j < msg.tool_calls.size(); ++j) {
                        if (j > 0) tools_called += u8str(u8", ");
                        tools_called += msg.tool_calls[j].name;
                    }
                    summary += u8str(u8"  [called tools: ") + tools_called + u8str(u8"]\n");
                }
                break;

            case MessageRole::Tool:
            {
                // 摘要中工具结果截断阈值：500 字符足以保留关键信息
                // 与在线工具结果截断（3000）协同：在线保留完整，摘要保留精简
                constexpr size_t kMaxToolResultLen = 500;
                u8str truncated = msg.content;
                u8str tool_id = msg.tool_call_id.value_or(u8str(u8""));
                if (truncated.size() > kMaxToolResultLen) {
                    size_t original_len = truncated.size();
                    std::string len_str = std::to_string(original_len);
                    truncated = truncated.substr(0, kMaxToolResultLen)
                              + u8str(u8"...[truncated, ")
                              + u8str(len_str.begin(), len_str.end())
                              + u8str(u8" chars total]");
                }
                summary += u8str(u8"Tool[") + tool_id + u8str(u8"]: ") + truncated + u8str(u8"\n");
                break;
            }

            case MessageRole::System:
                break;
        }
    }

    messages_.erase(messages_.begin(), messages_.begin() + static_cast<ptrdiff_t>(compress_count));

    Message compressed;
    compressed.role = MessageRole::System;
    compressed.content = u8str(u8"[Earlier conversation summary:\n") + summary + u8str(u8"]");
    messages_.insert(messages_.begin(), compressed);

    // 防御性清理：正常情况下上方切断点调整已避免孤立 Tool，
    // 此循环作为最后保险，清除任何残留的开头孤立 Tool 消息
    while (messages_.size() > 1 && messages_[1].role == MessageRole::Tool) {
        messages_.erase(messages_.begin() + 1);
    }
}

ContextSnapshot DefaultContextManager::get_snapshot(AgentState state,
                                             const std::vector<ThinkingStep>& thinking_steps,
                                             const std::optional<u8str>& final_output) const {
    std::shared_lock lock(mutex_);
    ContextSnapshot snapshot;
    snapshot.messages = messages_;
    snapshot.state = state;
    snapshot.thinking_steps = thinking_steps;
    snapshot.final_output = final_output;
    return snapshot;
}

void DefaultContextManager::truncate_to_messages(size_t count) {
    std::unique_lock lock(mutex_);
    if (count < messages_.size()) {
        messages_.resize(count);
    }
}

void DefaultContextManager::clear() {
    std::unique_lock lock(mutex_);
    messages_.clear();
}

size_t DefaultContextManager::message_count() const {
    std::shared_lock lock(mutex_);
    return messages_.size();
}

} // namespace agent