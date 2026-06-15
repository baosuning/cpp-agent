#include "llm_provider_factory.h"
#include "openai_provider.h"
#include "claude_provider.h"
#include "kimi_provider.h"
#include "deepseek_provider.h"
#include "glm_provider.h"

namespace agent {

LlmProviderPtr LlmProviderFactory::create(const LlmModelConfig& config) {
    switch (config.model_type) {
        case LlmModelType::OpenAI:
            return std::make_shared<OpenAIProvider>(config);
        case LlmModelType::Claude:
            return std::make_shared<ClaudeProvider>(config);
        case LlmModelType::Kimi:
            return std::make_shared<KimiProvider>(config);
        case LlmModelType::DeepSeek:
            return std::make_shared<DeepSeekProvider>(config);
        case LlmModelType::GLM:
            return std::make_shared<GlmProvider>(config);
        case LlmModelType::Custom:
            // Custom 类型使用 OpenAIProvider（OpenAI 兼容 API 格式），
            // 用户通过 config.api_base_url 指定自定义端点
            return std::make_shared<OpenAIProvider>(config);
    }
    return nullptr;
}

} // namespace agent
