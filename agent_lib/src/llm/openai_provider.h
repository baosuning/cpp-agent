#pragma once
// agent_lib/src/llm/openai_provider.h
#include "openai_compatible_provider.h"

namespace agent {

class OpenAIProvider : public OpenAICompatibleProvider {
public:
    explicit OpenAIProvider(const LlmModelConfig& config);

    u8str get_provider_name() const override;

protected:
    std::string get_default_base_url() const override;
};

} // namespace agent
