// agent_lib/src/llm/openai_provider.cpp
#include "openai_provider.h"

namespace agent {

OpenAIProvider::OpenAIProvider(const LlmModelConfig& config)
    : OpenAICompatibleProvider(config) {}

u8str OpenAIProvider::get_provider_name() const {
    return u8str(u8"OpenAI");
}

std::string OpenAIProvider::get_default_base_url() const {
    return "https://api.openai.com/v1";
}

} // namespace agent
