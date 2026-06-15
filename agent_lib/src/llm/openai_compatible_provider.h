#pragma once
// agent_lib/src/llm/openai_compatible_provider.h
// OpenAI 兼容 Provider 基类：为 OpenAI/DeepSeek/GLM/Kimi 等共享请求构建/响应解析/HTTP 调用逻辑
//
// 设计目标：
//   1. 消除 OpenAI/DeepSeek/GLM/Kimi 4 个 provider 80% 重复代码
//   2. 子类只需提供：端点、默认 base URL、是否解析 reasoning_content
//   3. Claude 协议不同，不继承此基类

#include <agent/i_llm_provider.h>
#include <util/i_http_client.h>
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <memory>
#include <optional>
#include <functional>
#include <thread>

namespace agent {

class OpenAICompatibleProvider : public ILlmProvider {
public:
    explicit OpenAICompatibleProvider(const LlmModelConfig& config);
    ~OpenAICompatibleProvider() override;

    LlmResponse send_request(const LlmRequest& request) override;
    void send_request_async(const LlmRequest& request,
                           std::function<void(LlmResponse)> callback) override;

protected:
    // === 子类必须实现的钩子 ===
    virtual std::string get_default_base_url() const = 0;
    u8str get_provider_name() const override = 0;  // 来自 ILlmProvider，纯虚

    // === 子类可选重写的钩子 ===
    // 是否在响应中解析 reasoning_content（DeepSeek/GLM 启用，OpenAI/Kimi 禁用）
    virtual bool supports_reasoning_content() const { return false; }

    // 是否在请求中发送 reasoning_content（GLM 启用以保留思维链）
    virtual bool include_reasoning_content_in_request() const { return false; }

    // 消息数组构建（含 system、tool_calls、tool_call_id）
    virtual nlohmann::json build_messages_array(const LlmRequest& request) const;

    // 响应解析（默认实现覆盖 OpenAI 协议大部分场景）
    virtual LlmResponse parse_response_json(const nlohmann::json& response_json) const;

    // 端点（默认 `/chat/completions`，子类可重写）
    virtual std::string get_chat_endpoint() const { return "/chat/completions"; }

    // 头部（默认 Bearer，可重写为自定义鉴权）
    virtual std::map<std::string, std::string> build_request_headers() const;

    LlmModelConfig               config_;
    std::unique_ptr<IHttpClient> http_client_;
};

}  // namespace agent
