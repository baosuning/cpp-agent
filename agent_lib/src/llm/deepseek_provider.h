#pragma once
// agent_lib/src/llm/deepseek_provider.h
#include "openai_compatible_provider.h"

namespace agent {

class DeepSeekProvider : public OpenAICompatibleProvider {
public:
    explicit DeepSeekProvider(const LlmModelConfig& config);

    u8str get_provider_name() const override;

protected:
    std::string get_default_base_url() const override;
    bool supports_reasoning_content() const override { return true; }
};

} // namespace agent
