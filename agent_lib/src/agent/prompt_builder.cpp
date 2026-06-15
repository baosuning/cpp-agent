#include "prompt_builder.h"
#include <agent/agent_config.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#include <winreg.h>
#else
#include <sys/utsname.h>
#endif

namespace agent {

std::string PromptBuilder::u8_to_str(const u8str& u8s) {
    return std::string(reinterpret_cast<const char*>(u8s.data()), u8s.size());
}

u8str PromptBuilder::str_to_u8(const std::string& s) {
    return u8str(reinterpret_cast<const char8_t*>(s.data()), s.size());
}

std::string PromptBuilder::get_os_info() {
    std::string os_info;

#ifdef _WIN32
    // 使用 Windows API 获取 codepage，避免 _popen 导致的管道错误
    int codepage = GetConsoleOutputCP();
    if (codepage <= 0) {
        codepage = 936;
    }

    // 使用注册表获取 Windows 版本信息，避免 _popen 导致的管道错误
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char productName[256] = {};
        DWORD productNameSize = sizeof(productName);
        RegQueryValueExA(hKey, "ProductName", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(productName), &productNameSize);

        char currentBuild[64] = {};
        DWORD buildSize = sizeof(currentBuild);
        RegQueryValueExA(hKey, "CurrentBuildNumber", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(currentBuild), &buildSize);

        RegCloseKey(hKey);

        os_info = std::string(productName);
        if (currentBuild[0] != '\0') {
            os_info += " Build " + std::string(currentBuild);
        }
    }

    if (os_info.empty()) {
        os_info = "Windows";
    }
#else
    struct utsname uname_data;
    if (uname(&uname_data) == 0) {
        os_info = std::string(uname_data.sysname) + " " +
                  std::string(uname_data.release) + " " +
                  std::string(uname_data.machine);
    } else {
        os_info = "Linux (version unknown)";
    }
#endif

    return os_info;
}

std::string PromptBuilder::get_current_date() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

u8str PromptBuilder::build_system_prompt(const PersonalityDocs& personality,
                                          const u8str& instruction) const {
    std::string prompt;

    if (!personality.soul.empty()) {
        prompt += "## Your Soul\n" + u8_to_str(personality.soul) + "\n\n";
    }

    if (!personality.identity.empty()) {
        prompt += "## Your Identity\n" + u8_to_str(personality.identity) + "\n\n";
    }

    if (!personality.agents.empty()) {
        prompt += "## Your Principles\n" + u8_to_str(personality.agents) + "\n\n";
    }

    if (!personality.skill_doc.empty()) {
        prompt += "## Available Skills\n\n";
        prompt += "**IMPORTANT: Skills are NOT callable functions. DO NOT call skill names directly. "
                  "You MUST use the `read_skill` tool to load a skill first.**\n\n";
        prompt += u8_to_str(personality.skill_doc) + "\n";
    }

    if (!personality.user_index.empty()) {
        prompt += "## User Index\n" + u8_to_str(personality.user_index) + "\n\n";
    }

    if (!personality.user_profiles.empty()) {
        prompt += "## User Profiles\n";
        for (const auto& [name, profile] : personality.user_profiles) {
            prompt += "### " + u8_to_str(name) + "\n" + u8_to_str(profile) + "\n\n";
        }
    }

    prompt += "## Search Priority\n";
    prompt += "For web search and information retrieval, the **anysearch** skill is the preferred source. "
              "Fall back to other search only when anysearch is unavailable.\n\n";

    prompt += "## System Environment\n";
    prompt += "- OS: " + get_os_info() + "\n";
    prompt += "- Current Date: " + get_current_date() + "\n\n";

    if (!instruction.empty()) {
        prompt += "## Instructions\n" + u8_to_str(instruction) + "\n\n";
    }

    return str_to_u8(prompt);
}

u8str PromptBuilder::build_user_prompt(const u8str& user_input,
                                        const PersonalityDocs& personality,
                                        const std::optional<u8str>& target_user) const {
    std::string prompt;

    if (target_user.has_value()) {
        auto it = personality.user_profiles.find(target_user.value());
        if (it != personality.user_profiles.end()) {
            prompt += "## About " + u8_to_str(target_user.value()) + "\n" + u8_to_str(it->second) + "\n\n";
        }
    }

    prompt += u8_to_str(user_input);

    return str_to_u8(prompt);
}

u8str PromptBuilder::build_tool_result_prompt(const ToolResult& result) const {
    std::string prompt;

    if (result.is_error) {
        prompt += "Tool Error (call_id: " + u8_to_str(result.tool_call_id) + "):\n" + u8_to_str(result.content);
    } else {
        prompt += "Tool Result (call_id: " + u8_to_str(result.tool_call_id) + "):\n" + u8_to_str(result.content);
    }

    return str_to_u8(prompt);
}

} // namespace agent