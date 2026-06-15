// agent_lib/src/llm/deepseek_provider.cpp
#include "deepseek_provider.h"

namespace agent {

DeepSeekProvider::DeepSeekProvider(const LlmModelConfig& config)
    : OpenAICompatibleProvider(config) {}

u8str DeepSeekProvider::get_provider_name() const {
    return u8str(u8"DeepSeek");
}

std::string DeepSeekProvider::get_default_base_url() const {
    return "https://api.deepseek.com/v1";
}

} // namespace agent
