#pragma once
#include <agent/types.h>
#include <agent/i_llm_provider.h>
#include <memory>

namespace agent {

class OpenAIProvider;
class ClaudeProvider;
class KimiProvider;
class DeepSeekProvider;
class GlmProvider;

class LlmProviderFactory {
public:
    static LlmProviderPtr create(const LlmModelConfig& config);
};

} // namespace agent
