// agent_lib/src/llm/kimi_provider.cpp
#include "kimi_provider.h"

namespace agent {

KimiProvider::KimiProvider(const LlmModelConfig& config)
    : OpenAICompatibleProvider(config) {}

u8str KimiProvider::get_provider_name() const {
    return u8str(u8"Kimi");
}

std::string KimiProvider::get_default_base_url() const {
    return "https://api.moonshot.cn/v1";
}

} // namespace agent
