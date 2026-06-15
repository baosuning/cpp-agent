#pragma once
// agent_lib/src/llm/glm_provider.h
#include "openai_compatible_provider.h"

namespace agent {

class GlmProvider : public OpenAICompatibleProvider {
public:
    explicit GlmProvider(const LlmModelConfig& config);

    u8str get_provider_name() const override;

protected:
    std::string get_default_base_url() const override;
    bool supports_reasoning_content() const override { return true; }
};

} // namespace agent
