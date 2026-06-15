#pragma once
#include <agent/i_llm_provider.h>
#include <thread>
#include <mutex>
#include <util/i_http_client.h>

namespace agent {

class ClaudeProvider : public ILlmProvider {
public:
    explicit ClaudeProvider(const LlmModelConfig& config);
    ~ClaudeProvider() override;

    LlmResponse send_request(const LlmRequest& request) override;
    void send_request_async(const LlmRequest& request,
                           std::function<void(LlmResponse)> callback) override;
    u8str get_provider_name() const override;

private:
    LlmModelConfig               config_;
    std::unique_ptr<IHttpClient>  http_client_;

    nlohmann::json build_request_body(const LlmRequest& request) const;
    LlmResponse parse_response(const nlohmann::json& response) const;
};

} // namespace agent
