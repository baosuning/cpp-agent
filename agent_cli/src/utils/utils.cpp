#include "utils.h"
#include <util/log.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace agent_cli {

std::string u8tostr(const agent::u8str& u8s) {
    return std::string(u8s.begin(), u8s.end());
}

agent::u8str strtou8(const std::string& s) {
    return agent::u8str(s.begin(), s.end());
}

#ifdef _WIN32
std::string gbk_to_utf8(const std::string& gbk) {
    if (gbk.empty()) return gbk;
    int len = MultiByteToWideChar(936, 0, gbk.c_str(), -1, nullptr, 0);
    if (len == 0) return gbk;
    std::wstring wstr(len - 1, L'\0');
    MultiByteToWideChar(936, 0, gbk.c_str(), -1, &wstr[0], len);
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8len == 0) return gbk;
    std::string result(utf8len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], utf8len, nullptr, nullptr);
    return result;
}
#endif

std::string read_file(const fs::path& path) {
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

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

YamlDoc parse_yaml_file(const fs::path& path) {
    YamlDoc doc;
    auto content = read_file(path);
    if (content.empty()) return doc;

    // Strip UTF-8 BOM (EF BB BF) if present
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
    }

    // Strip UTF-16 LE BOM (FF FE) if present
    if (content.size() >= 2 &&
        static_cast<unsigned char>(content[0]) == 0xFF &&
        static_cast<unsigned char>(content[1]) == 0xFE) {
        content = content.substr(2);
    }

    // Strip UTF-16 BE BOM (FE FF) if present
    if (content.size() >= 2 &&
        static_cast<unsigned char>(content[0]) == 0xFE &&
        static_cast<unsigned char>(content[1]) == 0xFF) {
        content = content.substr(2);
    }

    if (content.size() < 4 || content.substr(0, 3) != "---") {
        doc.body = content;
        return doc;
    }

    auto body_start = content.find('\n', 3);
    if (body_start == std::string::npos) return doc;

    auto end_marker = content.find("\n---", body_start + 1);
    if (end_marker == std::string::npos) {
        doc.body = content.substr(body_start + 1);
        return doc;
    }

    auto yaml_section = content.substr(body_start + 1, end_marker - body_start - 1);
    doc.body = content.substr(end_marker + 4);

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
            doc.front_matter[key] = value;
        }
    }

    return doc;
}

agent::LlmModelType parse_model_type(const std::string& type) {
    if (type == "OpenAI") return agent::LlmModelType::OpenAI;
    if (type == "Claude") return agent::LlmModelType::Claude;
    if (type == "Kimi") return agent::LlmModelType::Kimi;
    if (type == "DeepSeek") return agent::LlmModelType::DeepSeek;
    if (type == "GLM") return agent::LlmModelType::GLM;
    if (type == "Custom") return agent::LlmModelType::Custom;
    if (!type.empty()) {
        AGENT_LOG_WARN("Utils") << "Unknown model type '" << type
                  << "', expected one of: OpenAI, Claude, Kimi, DeepSeek, GLM, Custom. Using DeepSeek.";
    }
    return agent::LlmModelType::DeepSeek;
}

agent::u8str read_user_input_line(const std::string& prompt) {
    if (!prompt.empty()) {
        std::cout << prompt << std::flush;
    }
    std::string line;
    if (!std::getline(std::cin, line)) {
        return agent::u8str{};  // EOF
    }
#ifdef _WIN32
    std::string utf8 = gbk_to_utf8(line);
    return agent::u8str(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
#else
    return agent::u8str(reinterpret_cast<const char8_t*>(line.data()), line.size());
#endif
}

} // namespace agent_cli