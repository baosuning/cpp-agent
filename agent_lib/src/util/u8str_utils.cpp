// agent_lib/src/agent/u8str_utils.cpp
// u8str 工具函数的实现

#include <util/u8str_utils.h>
#include <nlohmann/json.hpp>
#include <mutex>

namespace agent::u8str_util {

// 全局浏览器工具名列表（线程安全）
// 工具识别规则是工具层概念，不属于 Agent 配置
namespace {
std::vector<std::string>& mutable_browser_launch_tools() {
    static std::vector<std::string> tools = {
        "cloak_launch",
        "browser_navigate",
    };
    return tools;
}
std::mutex& browser_tools_mutex() {
    static std::mutex m;
    return m;
}
}  // namespace

const std::vector<std::string>& default_browser_launch_tools() {
    // 返回一个包含所有内置默认工具的常量列表
    // 注意：不要返回 mutable_browser_launch_tools() 的引用，避免外部修改
    static const std::vector<std::string> defaults = {
        "cloak_launch",
        "browser_navigate",
    };
    return defaults;
}

void set_browser_launch_tools(std::vector<std::string> tool_names) {
    std::lock_guard<std::mutex> lock(browser_tools_mutex());
    mutable_browser_launch_tools() = std::move(tool_names);
}

std::vector<std::string> browser_launch_tools() {
    std::lock_guard<std::mutex> lock(browser_tools_mutex());
    return mutable_browser_launch_tools(); // 返回副本，锁释放后引用仍然有效
}

u8str override_headless_false(const u8str& args, const std::string& tool_name) {
    std::string orig_args(args.begin(), args.end());
    try {
        nlohmann::json j = nlohmann::json::parse(orig_args);
        j["headless"] = false;
        std::string new_args = j.dump();
        return u8str(new_args.begin(), new_args.end());
    } catch (...) {
        nlohmann::json j;
        j["headless"] = false;
        std::string new_args = j.dump();
        return u8str(new_args.begin(), new_args.end());
    }
}

bool is_stop_intent(const u8str& input) {
    if (input.empty()) return false;

    const u8str* patterns = detail::stop_intent_patterns();
    for (size_t i = 0; i < detail::stop_intent_patterns_count(); ++i) {
        if (input.find(patterns[i]) != u8str::npos) {
            return true;
        }
    }

    return false;
}

bool needs_user_input(const u8str& content) {
    if (content.empty()) return false;

    // 检测问号
    if (content.find(u8'?') != u8str::npos ||
        content.find(u8str(u8"？")) != u8str::npos) {
        return true;
    }

    // 检测中文模式
    const u8str* patterns = detail::user_input_patterns();
    for (size_t i = 0; i < 12; ++i) {
        if (content.find(patterns[i]) != u8str::npos) {
            return true;
        }
    }

    return false;
}

// 匿名命名空间：危险关键词检测（内部辅助）
namespace {
bool contains_dangerous_keywords(const std::string& text) {
    std::string lower;
    lower.reserve(text.size());
    for (char c : text) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    const char* dangerous_patterns[] = {
        "del ", "del\t", "del/", "delete ",
        "rd ", "rd\t", "rd/", "rmdir ",
        "rm ", "rm\t", "rm -", "rm/",
        "format ",
        "diskpart",
        "shutdown",
        "taskkill",
        "reg delete", "reg del",
        "clean", "prune"
    };

    for (const auto& pattern : dangerous_patterns) {
        if (lower.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}
}  // namespace

bool is_dangerous_command(const u8str& arguments) {
    std::string args(arguments.begin(), arguments.end());
    return contains_dangerous_keywords(args);
}

}  // namespace agent::u8str_util
