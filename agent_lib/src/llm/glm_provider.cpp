// agent_lib/src/llm/glm_provider.cpp
#include "glm_provider.h"

namespace agent {

GlmProvider::GlmProvider(const LlmModelConfig& config)
    : OpenAICompatibleProvider(config) {}

u8str GlmProvider::get_provider_name() const {
    return u8str(u8"GLM");
}

std::string GlmProvider::get_default_base_url() const {
    return "https://open.bigmodel.cn/api/paas/v4";
}

} // namespace agent
