#pragma once
#include "types.h"
#include "personality.h"
#include <memory>
#include <vector>

namespace agent {

class IPromptBuilder {
public:
    virtual ~IPromptBuilder() = default;

    virtual u8str build_system_prompt(const PersonalityDocs& personality,
                                       const u8str& instruction) const = 0;

    virtual u8str build_user_prompt(const u8str& user_input,
                                    const PersonalityDocs& personality,
                                    const std::optional<u8str>& target_user = std::nullopt) const = 0;

    virtual u8str build_tool_result_prompt(const ToolResult& result) const = 0;
};

using PromptBuilderPtr = std::shared_ptr<IPromptBuilder>;

} // namespace agent