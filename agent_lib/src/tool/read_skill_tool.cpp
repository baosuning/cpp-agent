#include "read_skill_tool.h"
#include "../skill/skill_scanner.h"
#include "../util/utf8_utils.h"
#include <nlohmann/json.hpp>

namespace agent {

static std::string u8_to_str(const u8str& s) {
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

static u8str str_to_u8(const std::string& s) {
    return u8str(reinterpret_cast<const char8_t*>(s.data()), s.size());
}

ReadSkillTool::ReadSkillTool(std::shared_ptr<SkillScanner> scanner)
    : scanner_(std::move(scanner)) {}

u8str ReadSkillTool::name() const {
    return str_to_u8("read_skill");
}

u8str ReadSkillTool::description() const {
    return str_to_u8("Read the full instructions for a skill by name. Use this tool FIRST to load a skill's complete instructions before attempting to execute it. Returns the complete SKILL.md content including YAML configuration and markdown instructions.");
}

u8str ReadSkillTool::parameters_schema() const {
    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"]["skill_name"]["type"] = "string";
    schema["properties"]["skill_name"]["description"] = "The name of the skill to load (see Available Skills list in system prompt)";

    schema["required"] = nlohmann::json::array({"skill_name"});
    return str_to_u8(schema.dump());
}

u8str ReadSkillTool::execute(const u8str& arguments) {
    std::string args_str = u8_to_str(llm::sanitize_utf8(arguments));

    try {
        auto args_json = nlohmann::json::parse(args_str);
        std::string skill_name;

        if (args_json.is_string()) {
            skill_name = args_json.get<std::string>();
        } else if (args_json.is_object() && args_json.contains("skill_name")) {
            skill_name = args_json["skill_name"].get<std::string>();
        } else {
            return str_to_u8("Error: Please provide a 'skill_name' argument");
        }

        if (!scanner_) {
            return str_to_u8("Error: Skill scanner is not available");
        }

        return str_to_u8(scanner_->read_skill(skill_name));
    } catch (const nlohmann::json::parse_error&) {
        std::string trimmed = args_str;
        trimmed.erase(0, trimmed.find_first_not_of(" \t\"'"));
        trimmed.erase(trimmed.find_last_not_of(" \t\"'") + 1);
        if (!trimmed.empty() && scanner_ && scanner_->has_skill(trimmed)) {
            return str_to_u8(scanner_->read_skill(trimmed));
        }
        return str_to_u8("Error: Invalid arguments. Please provide a JSON object with 'skill_name' field.");
    }
}

void ReadSkillTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool ReadSkillTool::requires_confirmation() const {
    return false;
}

} // namespace agent