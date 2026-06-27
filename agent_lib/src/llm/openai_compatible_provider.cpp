// agent_lib/src/llm/openai_compatible_provider.cpp
// OpenAI 兼容 Provider 基类实现

#include "openai_compatible_provider.h"
#include "../util/utf8_utils.h"
#include <util/log.h>
#include <util/u8str_utils.h>

namespace agent {

using agent::llm::sanitize_utf8;

OpenAICompatibleProvider::OpenAICompatibleProvider(const LlmModelConfig& config)
    : config_(config), http_client_(create_http_client()) {}

OpenAICompatibleProvider::~OpenAICompatibleProvider() = default;

std::map<std::string, std::string> OpenAICompatibleProvider::build_request_headers() const {
    std::map<std::string, std::string> headers;
    std::string api_key = u8str_util::to_string(config_.api_key);
    headers["Authorization"] = "Bearer " + api_key;
    return headers;
}

nlohmann::json OpenAICompatibleProvider::build_messages_array(const LlmRequest& request) const {
    nlohmann::json messages = nlohmann::json::array();

    if (request.system_prompt.has_value() && !request.system_prompt->empty()) {
        u8str clean_sys = sanitize_utf8(request.system_prompt.value());
        std::string sys = u8str_util::to_string(clean_sys);
        messages.push_back({{"role", "system"}, {"content", sys}});
    }

    for (const auto& msg : request.messages) {
        std::string role;
        switch (msg.role) {
            case MessageRole::System:    role = "system";    break;
            case MessageRole::User:      role = "user";      break;
            case MessageRole::Assistant: role = "assistant"; break;
            case MessageRole::Tool:      role = "tool";      break;
        }

        u8str clean_content = sanitize_utf8(msg.content);
        std::string content = u8str_util::to_string(clean_content);

        nlohmann::json msg_obj;
        msg_obj["role"] = role;
        msg_obj["content"] = content;

        if (msg.role == MessageRole::Assistant && !msg.tool_calls.empty()) {
            nlohmann::json tc_array = nlohmann::json::array();
            for (const auto& tc : msg.tool_calls) {
                nlohmann::json tc_obj;
                std::string tc_id    = u8str_util::to_string(sanitize_utf8(tc.id));
                std::string tc_name  = u8str_util::to_string(sanitize_utf8(tc.name));
                std::string tc_args  = u8str_util::to_string(sanitize_utf8(tc.arguments));
                tc_obj["id"] = tc_id;
                tc_obj["type"] = "function";
                tc_obj["function"]["name"] = tc_name;
                tc_obj["function"]["arguments"] = tc_args;
                tc_array.push_back(std::move(tc_obj));
            }
            msg_obj["tool_calls"] = tc_array;
        }

        if (msg.role == MessageRole::Tool && msg.tool_call_id.has_value()) {
            std::string tcid = u8str_util::to_string(sanitize_utf8(msg.tool_call_id.value()));
            msg_obj["tool_call_id"] = tcid;
        }

        if (msg.name.has_value()) {
            std::string name = u8str_util::to_string(sanitize_utf8(msg.name.value()));
            msg_obj["name"] = name;
        }

        messages.push_back(msg_obj);
    }

    return messages;
}

LlmResponse OpenAICompatibleProvider::parse_response_json(const nlohmann::json& response) const {
    LlmResponse result;

    try {
        if (response.contains("error")) {
            result.is_error = true;
            if (response["error"].contains("message")) {
                std::string msg = response["error"]["message"].get<std::string>();
                result.error_message = u8str_util::to_u8str(msg);
            } else {
                result.error_message = u8str(u8"Unknown API error");
            }
            return result;
        }

        if (!response.contains("choices") || response["choices"].empty()) {
            result.is_error = true;
            result.error_message = u8str(u8"No choices in response");
            return result;
        }

        const auto& message = response["choices"][0]["message"];

        if (message.contains("content") && !message["content"].is_null()) {
            std::string content = message["content"].get<std::string>();
            result.content = u8str_util::to_u8str(content);
        }

        if (supports_reasoning_content() &&
            message.contains("reasoning_content") && !message["reasoning_content"].is_null()) {
            std::string reasoning = message["reasoning_content"].get<std::string>();
            result.thinking_content = u8str_util::to_u8str(reasoning);
        }

        if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
            for (const auto& tc : message["tool_calls"]) {
                ToolCall tool_call;
                if (tc.contains("id")) {
                    std::string id = tc["id"].get<std::string>();
                    tool_call.id = u8str_util::to_u8str(id);
                }
                if (tc.contains("function")) {
                    if (tc["function"].contains("name")) {
                        std::string name = tc["function"]["name"].get<std::string>();
                        tool_call.name = u8str_util::to_u8str(name);
                    }
                    if (tc["function"].contains("arguments")) {
                        std::string args = tc["function"]["arguments"].get<std::string>();
                        tool_call.arguments = u8str_util::to_u8str(args);
                    }
                }
                result.tool_calls.push_back(std::move(tool_call));
            }
        }
    } catch (const std::exception& e) {
        result.is_error = true;
        std::string msg = std::string("[Parse Error] ") + e.what();
        result.error_message = u8str_util::to_u8str(msg);
    }

    // 提取 usage 字段（OpenAI 协议：prompt_tokens / completion_tokens / total_tokens）
    if (!result.is_error && response.contains("usage") && response["usage"].is_object()) {
        TokenUsage usage;
        usage.prompt_tokens     = response["usage"].value("prompt_tokens", 0);
        usage.completion_tokens = response["usage"].value("completion_tokens", 0);
        // 统一使用 prompt + completion 作为 total，避免不同 API 的 total 口径不一致
        usage.total_tokens      = usage.prompt_tokens + usage.completion_tokens;
        result.usage = usage;
    }

    return result;
}

LlmResponse OpenAICompatibleProvider::send_request(const LlmRequest& request) {
    LlmResponse result;

    try {
        // 构建请求体
        nlohmann::json body;
        std::string model_name = u8str_util::to_string(config_.model_name);
        body["model"] = model_name;
        body["messages"] = build_messages_array(request);
        body["temperature"] = config_.temperature;
        body["max_tokens"] = config_.max_tokens;
        body["top_p"] = config_.top_p;

        if (!request.tools.empty()) {
            body["tools"] = request.tools;
        }

        std::string body_str = body.dump();

        // 构建端点
        std::string base = u8str_util::to_string(config_.api_base_url);
        if (base.empty()) {
            base = get_default_base_url();
        }
        if (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        std::string endpoint = base + get_chat_endpoint();

        // 发起 HTTP 请求
        auto headers = build_request_headers();
        HttpResponse http_resp = http_client_->post(endpoint, body_str, headers);

        if (http_resp.is_error) {
            result.is_error = true;
            result.error_message = u8str(u8"[HTTP Error] ") +
                u8str_util::to_u8str(http_resp.error_message);
            return result;
        }

        if (http_resp.status_code != 200) {
            result.is_error = true;
            std::string clean_body = llm::sanitize_utf8_string(http_resp.body);
            std::string msg = "[HTTP " + std::to_string(http_resp.status_code) + "] " + clean_body;
            result.error_message = u8str_util::to_u8str(msg);
            return result;
        }

        try {
            std::string clean_body = llm::sanitize_utf8_string(http_resp.body);
            nlohmann::json response_json = nlohmann::json::parse(clean_body);
            result = parse_response_json(response_json);
        } catch (const std::exception& e) {
            result.is_error = true;
            std::string msg = std::string("[JSON Parse Error] ") + e.what();
            result.error_message = u8str_util::to_u8str(msg);
        }
    } catch (const std::exception& e) {
        result.is_error = true;
        std::string msg = std::string("[Request Error] ") + e.what();
        result.error_message = u8str_util::to_u8str(msg);
    }

    return result;
}

void OpenAICompatibleProvider::send_request_async(
    const LlmRequest& request,
    std::function<void(LlmResponse)> callback) {
    // Safe synchronous implementation (no detach risk).
    // When true async is needed, refactor to capture shared_ptr<self>.
    callback(send_request(request));
}

}  // namespace agent
