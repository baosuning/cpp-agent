#include "skill_scanner.h"
#include <fstream>
#include <sstream>

namespace agent {

void SkillScanner::scan_skills(const fs::path& skills_dir) {
    // 不调用 clear()，允许累积扫描多个目录

    if (!fs::is_directory(skills_dir)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(skills_dir)) {
        if (!entry.is_directory()) continue;

        fs::path skill_file = entry.path() / "SKILL.md";
        if (!fs::exists(skill_file)) continue;

        auto content = read_file(skill_file);
        if (content.empty()) continue;

        std::string body;
        auto front_matter = parse_front_matter(content, body);
        if (!front_matter.count("name") || !front_matter.count("description")) {
            continue;
        }

        SkillInfo info;
        info.name = front_matter["name"];
        info.description = front_matter["description"];
        info.file_path = skill_file;
        skills_[info.name] = std::move(info);
    }
}

std::string SkillScanner::build_catalog() const {
    if (skills_.empty()) {
        return {};
    }

    std::string catalog = "## Available Skills\n";
    catalog += "You have the following skills available. To use a skill, call the `read_skill` tool with the skill name to load its full instructions.\n\n";
    for (const auto& [name, info] : skills_) {
        catalog += "- **" + name + "**: " + info.description + "\n";
    }
    catalog += "\nTo use a skill:\n";
    catalog += "1. Call `read_skill` with the skill name (e.g., `read_skill(\"skill_name\")`)\n";
    catalog += "2. Read the returned instructions carefully\n";
    catalog += "3. Follow the instructions step by step\n";

    return catalog;
}

std::string SkillScanner::read_skill(const std::string& name) const {
    auto it = skills_.find(name);
    if (it == skills_.end()) {
        return "Error: Skill '" + name + "' not found. Available skills: " + list_skill_names();
    }
    return read_file(it->second.file_path);
}

bool SkillScanner::has_skill(const std::string& name) const {
    return skills_.find(name) != skills_.end();
}

std::vector<SkillInfo> SkillScanner::list_skills() const {
    std::vector<SkillInfo> result;
    result.reserve(skills_.size());
    for (const auto& [_, info] : skills_) {
        result.push_back(info);
    }
    return result;
}

std::string SkillScanner::list_skill_names() const {
    std::string names;
    for (const auto& [name, _] : skills_) {
        if (!names.empty()) names += ", ";
        names += name;
    }
    return names;
}

std::string SkillScanner::read_file(const fs::path& path) const {
    std::ifstream file(path);
    if (!file.is_open()) return {};
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto s = buffer.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

std::string SkillScanner::trim(const std::string& s) const {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::map<std::string, std::string> SkillScanner::parse_front_matter(const std::string& content, std::string& body) const {
    std::map<std::string, std::string> result;

    if (content.size() < 4 || content.substr(0, 3) != "---") {
        body = content;
        return result;
    }

    auto body_start = content.find('\n', 3);
    if (body_start == std::string::npos) {
        body = content;
        return result;
    }

    auto end_marker = content.find("\n---", body_start + 1);
    if (end_marker == std::string::npos) {
        body = content.substr(body_start + 1);
        return result;
    }

    auto yaml_section = content.substr(body_start + 1, end_marker - body_start - 1);
    body = content.substr(end_marker + 4);

    std::istringstream stream(yaml_section);
    std::string line;
    while (std::getline(stream, line)) {
        auto trimmed = trim(line);
        if (trimmed.empty()) continue;
        auto colon = trimmed.find(':');
        if (colon == std::string::npos) continue;
        auto key = trim(trimmed.substr(0, colon));
        auto value = trim(trimmed.substr(colon + 1));
        if (!key.empty()) {
            result[key] = value;
        }
    }

    return result;
}

} // namespace agent