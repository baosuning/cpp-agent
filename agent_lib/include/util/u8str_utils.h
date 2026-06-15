#pragma once
// agent_lib/include/util/u8str_utils.h
// UTF-8 字符串工具：提供 std::string 与 agent::u8str 之间的零拷贝/低开销转换
//
// 设计目标：
//   1. 减少全项目 100+ 处 `std::string(u8s.begin(), u8s.end())` 重复
//   2. 提供 `is_browser_launch_tool` 等跨 Loop 共享工具（消除 react_loop/plan_execute_loop 重复）
//   3. 提供 `needs_user_input` 文本模式匹配（消除 agent_impl.cpp/ReactLoop 重复）
//   4. 与现有 `PromptBuilder::u8_to_str/str_to_u8` 兼容，但作为自由函数暴露

#include <agent/types.h>
#include <string>
#include <string_view>

namespace agent::u8str_util {

// u8str -> std::string（高效转换）
inline std::string to_string(const u8str& s) {
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// std::string -> u8str（高效转换）
inline u8str to_u8str(const std::string& s) {
    return u8str(reinterpret_cast<const char8_t*>(s.data()), s.size());
}

// std::string_view -> u8str（避免拷贝）
inline u8str to_u8str(std::string_view s) {
    return u8str(reinterpret_cast<const char8_t*>(s.data()), s.size());
}

// 字符串字面量 -> u8str
inline u8str to_u8str(const char* s) {
    return u8str(reinterpret_cast<const char8_t*>(s), std::char_traits<char>::length(s));
}

// 拼接辅助：u8str + std::string
inline u8str concat(const u8str& a, const std::string& b) {
    u8str r(a);
    r.append(reinterpret_cast<const char8_t*>(b.data()), b.size());
    return r;
}

// 拼接辅助：u8str + char*
inline u8str concat(const u8str& a, const char* b) {
    u8str r(a);
    r.append(reinterpret_cast<const char8_t*>(b));
    return r;
}

// 拼接辅助：u8str + u8str
inline u8str concat(const u8str& a, const u8str& b) {
    u8str r(a);
    r.append(b);
    return r;
}

// 截取子串并转换为 std::string
inline std::string substring_as_string(const u8str& s, size_t pos, size_t len) {
    if (pos >= s.size()) return {};
    size_t actual_len = std::min(len, s.size() - pos);
    return std::string(reinterpret_cast<const char*>(s.data() + pos), actual_len);
}

// ============================================================
// 跨 Loop 共享工具（消除 react_loop/plan_execute_loop 重复）
// ============================================================

// 获取默认浏览器工具名列表（程序启动时的初始值）
// 返回常量引用，避免拷贝
const std::vector<std::string>& default_browser_launch_tools();

// 设置浏览器工具名列表（全局影响所有 Agent 实例）
// 工具识别规则是工具层概念，不应混入 Agent 配置
// 调用方应在 Agent::create() 之前调用
void set_browser_launch_tools(std::vector<std::string> tool_names);

// 获取当前浏览器工具名列表（用户/程序运行时设置的）
// 返回副本，线程安全
std::vector<std::string> browser_launch_tools();

// 判断是否浏览器启动类工具（需要强制 headless=false）
// 通过全局配置的列表 + 启发式后缀匹配，避免硬编码
inline bool is_browser_launch_tool(const std::string& tool_name) {
    // 1. 精确匹配配置列表（用户通过 set_browser_launch_tools 设置）
    for (const auto& name : browser_launch_tools()) {
        if (tool_name == name) return true;
    }
    // 2. 启发式：包含 launch/navigate 的浏览器工具
    if (tool_name.find("_launch") != std::string::npos) return true;
    if (tool_name.find("_navigate") != std::string::npos) return true;
    if (tool_name.find("browser_launch") != std::string::npos) return true;
    if (tool_name.find("playwright_") == 0 && tool_name.find("launch") != std::string::npos) return true;
    return false;
}

// 对浏览器启动工具强制设置 headless=false
// args: 工具参数（JSON 字符串）
// tool_name: 工具名称
// 返回: 修改后的 JSON 字符串
u8str override_headless_false(const u8str& args, const std::string& tool_name);

// ============================================================
// 跨 Loop 共享：检测用户输入是否包含停止意图
// ============================================================

// 停止意图模式列表
namespace detail {
inline const u8str* stop_intent_patterns() {
    static const u8str patterns[] = {
        u8str(u8"不用了"), u8str(u8"算了"), u8str(u8"不要了"),
        u8str(u8"不需要"), u8str(u8"停"), u8str(u8"停止"),
        u8str(u8"取消"), u8str(u8"cancel"), u8str(u8"stop"),
        u8str(u8"够了"), u8str(u8"结束"), u8str(u8"不用"),
        u8str(u8"别了"), u8str(u8"作罢"), u8str(u8"放弃"),
    };
    return patterns;
}
inline constexpr size_t stop_intent_patterns_count() {
    return 15;
}
}  // namespace detail

// 检测用户输入是否包含停止意图（用于 auto-continue 判断）
bool is_stop_intent(const u8str& input);

// ============================================================
// 跨 Loop 共享：检测 LLM 输出是否需要用户输入
// ============================================================

// 中文模式列表（共享）
namespace detail {
inline const u8str* user_input_patterns() {
    static const u8str patterns[] = {
        u8str(u8"请输入"), u8str(u8"请填入"), u8str(u8"请提供"),
        u8str(u8"请确认"), u8str(u8"请选择"), u8str(u8"需要你"),
        u8str(u8"需要您"), u8str(u8"告诉我"), u8str(u8"请回复"),
        u8str(u8"请回答"), u8str(u8"请等待"), u8str(u8"请查收"),
    };
    return patterns;
}
inline constexpr size_t user_input_patterns_count() {
    return 12;
}
}  // namespace detail

// 检测内容是否包含 "需要用户输入" 模式（共享给 ReactLoop::needs_user_input 和 Agent::Impl）
bool needs_user_input(const u8str& content);

// ============================================================
// 跨 Loop 共享：检测危险命令（消除 react_loop 独占的安全检查）
// ============================================================

// 检测工具参数中是否包含危险操作（rm -rf / format / shutdown 等）
bool is_dangerous_command(const u8str& arguments);

}  // namespace agent::u8str_util
