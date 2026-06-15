#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace agent {

namespace fs = std::filesystem;

struct SkillInfo {
    std::string name;
    std::string description;
    fs::path file_path;
};

class SkillScanner {
public:
    // 扫描指定目录下的 SKILL.md 文件，累积到内部 skill 列表
    // 多次调用会累积（不覆盖），如需清空请创建新的 SkillScanner
    void scan_skills(const fs::path& skills_dir);
    std::string build_catalog() const;
    std::string read_skill(const std::string& name) const;
    bool has_skill(const std::string& name) const;
    std::vector<SkillInfo> list_skills() const;

private:
    std::string list_skill_names() const;
    std::string read_file(const fs::path& path) const;
    std::map<std::string, std::string> parse_front_matter(const std::string& content, std::string& body) const;
    std::string trim(const std::string& s) const;
    std::map<std::string, SkillInfo> skills_;
};

} // namespace agent