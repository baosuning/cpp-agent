#pragma once

#include <agent/types.h>
#include <filesystem>
#include <string>
#include <map>

namespace agent_cli {

namespace fs = std::filesystem;

std::string u8tostr(const agent::u8str& u8s);
agent::u8str strtou8(const std::string& s);

#ifdef _WIN32
std::string gbk_to_utf8(const std::string& gbk);
#endif

std::string read_file(const fs::path& path);
std::string trim(const std::string& s);

struct YamlDoc {
    std::map<std::string, std::string> front_matter;
    std::string body;
};

YamlDoc parse_yaml_file(const fs::path& path);

agent::LlmModelType parse_model_type(const std::string& type);

// 读取用户输入并转换为 UTF-8 编码的 u8str
agent::u8str read_user_input_line(const std::string& prompt = "> ");

} // namespace agent_cli