#pragma once
// agent_lib/src/llm/kimi_provider.h
#include "openai_compatible_provider.h"

namespace agent {

class KimiProvider : public OpenAICompatibleProvider {
public:
    explicit KimiProvider(const LlmModelConfig& config);

    u8str get_provider_name() const override;

protected:
    std::string get_default_base_url() const override;
};

} // namespace agent
