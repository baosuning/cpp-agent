#pragma once
#include <agent/i_prompt_builder.h>
#include <string>

namespace agent {

class PromptBuilder : public IPromptBuilder {
public:
    u8str build_system_prompt(const PersonalityDocs& personality,
                               const u8str& instruction) const override;

    u8str build_user_prompt(const u8str& user_input,
                            const PersonalityDocs& personality,
                            const std::optional<u8str>& target_user = std::nullopt) const override;

    u8str build_tool_result_prompt(const ToolResult& result) const override;

private:
    static std::string u8_to_str(const u8str& u8s);
    static u8str str_to_u8(const std::string& s);
    static std::string get_os_info();
    static std::string get_current_date();
};

} // namespace agent