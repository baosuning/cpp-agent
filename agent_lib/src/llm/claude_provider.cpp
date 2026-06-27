#include "claude_provider.h"
#include "../util/utf8_utils.h"

namespace agent {

using agent::llm::sanitize_utf8;

ClaudeProvider::ClaudeProvider(const LlmModelConfig& config)
    : config_(config), http_client_(create_http_client()) {}

ClaudeProvider::~ClaudeProvider() = default;

u8str ClaudeProvider::get_provider_name() const {
    return u8str(u8"Claude");
}

nlohmann::json ClaudeProvider::build_request_body(const LlmRequest& request) const {
    nlohmann::json body;

    std::string model_name(reinterpret_cast<const char*>(config_.model_name.c_str()),
                           config_.model_name.size());
    body["model"] = model_name;
    body["max_tokens"] = config_.max_tokens;
    body["temperature"] = config_.temperature;

    if (request.system_prompt.has_value() && !request.system_prompt->empty()) {
        u8str clean_sys = sanitize_utf8(request.system_prompt.value());
        std::string sys(reinterpret_cast<const char*>(clean_sys.c_str()),
                       clean_sys.size());
        body["system"] = sys;
    }

    // 加入 tools（Claude API 格式）
    if (!request.tools.empty()) {
        body["tools"] = request.tools;
    }

    nlohmann::json messages = nlohmann::json::array();
    for (const auto& msg : request.messages) {
        std::string role;
        switch (msg.role) {
            case MessageRole::User:      role = "user";      break;
            case MessageRole::Assistant: role = "assistant"; break;
            case MessageRole::Tool:      role = "user";      break;  // Claude 用 user + tool_result
            default: continue;
        }

        nlohmann::json msg_obj;
        msg_obj["role"] = role;

        // Tool 消息：Claude 格式为 user role + tool_result content block
        if (msg.role == MessageRole::Tool) {
            nlohmann::json content_array = nlohmann::json::array();
            nlohmann::json tool_result_block;
            tool_result_block["type"] = "tool_result";
            if (msg.tool_call_id.has_value()) {
                std::string tcid(reinterpret_cast<const char*>(msg.tool_call_id->c_str()),
                                 msg.tool_call_id->size());
                tool_result_block["tool_use_id"] = tcid;
            }
            u8str clean_content = sanitize_utf8(msg.content);
            std::string content(reinterpret_cast<const char*>(clean_content.c_str()),
                               clean_content.size());
            tool_result_block["content"] = content;
            content_array.push_back(std::move(tool_result_block));
            msg_obj["content"] = content_array;
            messages.push_back(msg_obj);
            continue;
        }

        // Assistant 消息带 tool_calls：Claude 格式为 content blocks 数组
        if (msg.role == MessageRole::Assistant && !msg.tool_calls.empty()) {
            nlohmann::json content_array = nlohmann::json::array();

            // 文本内容
            if (!msg.content.empty()) {
                u8str clean_content = sanitize_utf8(msg.content);
                std::string content(reinterpret_cast<const char*>(clean_content.c_str()),
                                   clean_content.size());
                nlohmann::json text_block;
                text_block["type"] = "text";
                text_block["text"] = content;
                content_array.push_back(std::move(text_block));
            }

            // tool_use blocks
            for (const auto& tc : msg.tool_calls) {
                nlohmann::json tool_block;
                tool_block["type"] = "tool_use";
                u8str clean_id = sanitize_utf8(tc.id);
                std::string tc_id(reinterpret_cast<const char*>(clean_id.c_str()),
                                  clean_id.size());
                tool_block["id"] = tc_id;
                u8str clean_name = sanitize_utf8(tc.name);
                std::string tc_name(reinterpret_cast<const char*>(clean_name.c_str()),
                                    clean_name.size());
                tool_block["name"] = tc_name;
                // 解析 arguments 为 JSON 对象
                u8str clean_args = sanitize_utf8(tc.arguments);
                std::string tc_args(reinterpret_cast<const char*>(clean_args.c_str()),
                                    clean_args.size());
                try {
                    tool_block["input"] = nlohmann::json::parse(tc_args);
                } catch (...) {
                    tool_block["input"] = nlohmann::json::object();
                }
                content_array.push_back(std::move(tool_block));
            }

            msg_obj["content"] = content_array;
            messages.push_back(msg_obj);
            continue;
        }

        // 普通文本消息
        u8str clean_content = sanitize_utf8(msg.content);
        std::string content(reinterpret_cast<const char*>(clean_content.c_str()),
                           clean_content.size());
        msg_obj["content"] = content;
        messages.push_back(msg_obj);
    }

    body["messages"] = messages;

    return body;
}

LlmResponse ClaudeProvider::parse_response(const nlohmann::json& response) const {
    LlmResponse result;

    try {
        if (response.contains("error")) {
            result.is_error = true;
            if (response["error"].contains("message")) {
                std::string msg = response["error"]["message"].get<std::string>();
                result.error_message = u8str(msg.begin(), msg.end());
            } else {
                result.error_message = u8str(u8"Unknown API error");
            }
            return result;
        }

        if (!response.contains("content") || !response["content"].is_array()) {
            result.is_error = true;
            result.error_message = u8str(u8"No content in response");
            return result;
        }

        for (const auto& block : response["content"]) {
            std::string type = block.value("type", "");

            if (type == "text") {
                std::string text = block.value("text", "");
                result.content = u8str(text.begin(), text.end());
            } else if (type == "thinking") {
                if (block.contains("thinking")) {
                    std::string thinking = block["thinking"].get<std::string>();
                    result.thinking_content = u8str(thinking.begin(), thinking.end());
                }
            } else if (type == "tool_use") {
                ToolCall tool_call;
                if (block.contains("id")) {
                    std::string id = block["id"].get<std::string>();
                    tool_call.id = u8str(id.begin(), id.end());
                }
                if (block.contains("name")) {
                    std::string name = block["name"].get<std::string>();
                    tool_call.name = u8str(name.begin(), name.end());
                }
                if (block.contains("input")) {
                    std::string input = block["input"].dump();
                    tool_call.arguments = u8str(input.begin(), input.end());
                }
                result.tool_calls.push_back(std::move(tool_call));
            }
        }
    } catch (const std::exception& e) {
        result.is_error = true;
        std::string msg = std::string("[Parse Error at ") + __FILE__ + ":" + std::to_string(__LINE__) + "] " + e.what();
        result.error_message = u8str(msg.begin(), msg.end());
    }

    // 提取 usage 字段（Anthropic 协议：input_tokens / output_tokens）
    if (!result.is_error && response.contains("usage") && response["usage"].is_object()) {
        TokenUsage usage;
        usage.prompt_tokens     = response["usage"].value("input_tokens", 0);
        usage.completion_tokens = response["usage"].value("output_tokens", 0);
        usage.total_tokens      = usage.prompt_tokens + usage.completion_tokens;
        result.usage = usage;
    }

    return result;
}

LlmResponse ClaudeProvider::send_request(const LlmRequest& request) {
    LlmResponse result;

    try {
        nlohmann::json body = build_request_body(request);
        std::string body_str = body.dump();

        std::string base(reinterpret_cast<const char*>(config_.api_base_url.c_str()),
                        config_.api_base_url.size());
        if (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        std::string endpoint = base + "/messages";

        std::string api_key(reinterpret_cast<const char*>(config_.api_key.c_str()),
                           config_.api_key.size());

        std::map<std::string, std::string> headers;
        headers["x-api-key"] = api_key;
        headers["anthropic-version"] = "2023-06-01";

        HttpResponse http_resp = http_client_->post(endpoint, body_str, headers);

        if (http_resp.is_error) {
            result.is_error = true;
            result.error_message = u8str(http_resp.error_message.begin(),
                                         http_resp.error_message.end());
            return result;
        }

        if (http_resp.status_code != 200) {
            result.is_error = true;
            std::string msg = "HTTP error: " + std::to_string(http_resp.status_code);
            if (!http_resp.body.empty()) {
                msg += " - " + agent::llm::sanitize_utf8_string(http_resp.body);
            }
            result.error_message = u8str(msg.begin(), msg.end());
            return result;
        }

        std::string clean_body = llm::sanitize_utf8_string(http_resp.body);
        nlohmann::json response_json = nlohmann::json::parse(clean_body);
        result = parse_response(response_json);
    } catch (const std::exception& e) {
        result.is_error = true;
        std::string msg = std::string("[Request Error at ") + __FILE__ + ":" + std::to_string(__LINE__) + "] " + e.what();
        result.error_message = u8str(msg.begin(), msg.end());
    }

    return result;
}

void ClaudeProvider::send_request_async(const LlmRequest& request,
                                         std::function<void(LlmResponse)> callback) {
    // Safe synchronous implementation (no detach risk).
    // When true async is needed, refactor to capture shared_ptr<self>.
    callback(send_request(request));
}

} // namespace agent
