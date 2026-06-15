#pragma once
// agent_lib/include/util/log.h
// 统一日志模块：替换所有 PE_DEBUG/REACT_DEBUG/DP_DEBUG 宏，提供分级日志
//
// 用法：
//   AGENT_LOG_DEBUG("Agent") << "Step " << step << ": calling LLM";
//   AGENT_LOG_INFO("Agent") << "Connected to " << url;
//   AGENT_LOG_WARN("MCP") << "Connection retry...";
//   AGENT_LOG_ERROR("MCP") << "Failed: " << e.what();
//
// 日志级别通过 agent::log::set_level() 全局控制
// 默认级别由 AgentConfig::debug 决定

#include <agent/types.h>
#include <sstream>
#include <string>
#include <string_view>
#include <iostream>
#include <mutex>

namespace agent::log {

enum class Level {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    Off   = 4
};

// 全局日志级别控制（线程安全）
void set_level(Level level);
Level get_level();

// 是否输出到 stderr（默认 false=stdout）
void set_output_to_stderr(bool enable);
bool is_output_to_stderr();

// 内部：LogStream 类（用于 << 链式调用）
class LogStream {
public:
    LogStream(Level level, const char* module);
    ~LogStream();

    template<typename T>
    LogStream& operator<<(const T& value) {
        if (enabled_) {
            stream_ << value;
        }
        return *this;
    }

    LogStream& operator<<(const u8str& value) {
        if (enabled_) {
            stream_ << std::string(reinterpret_cast<const char*>(value.data()), value.size());
        }
        return *this;
    }

    LogStream& operator<<(const char8_t* value) {
        if (enabled_ && value) {
            stream_ << std::string(reinterpret_cast<const char*>(value));
        }
        return *this;
    }

    LogStream& operator<<(std::string_view value) {
        if (enabled_) {
            stream_ << value;
        }
        return *this;
    }

private:
    bool   enabled_;
    Level  level_;
    const char* module_;
    std::ostringstream stream_;
    static std::mutex mutex_;
};

// 便捷宏
#define AGENT_LOG_DEBUG(module) ::agent::log::LogStream(::agent::log::Level::Debug, module)
#define AGENT_LOG_INFO(module)  ::agent::log::LogStream(::agent::log::Level::Info,  module)
#define AGENT_LOG_WARN(module)  ::agent::log::LogStream(::agent::log::Level::Warn,  module)
#define AGENT_LOG_ERROR(module) ::agent::log::LogStream(::agent::log::Level::Error, module)

// 兼容旧宏的转换宏（逐步迁移）
// 用法：AGENT_DEBUG_OLD(debug_flag) << "...";
// 仅当 debug_flag 为 true 时输出 Debug 级别
#define AGENT_LOG_DEBUG_IF(cond, module) \
    ::agent::log::LogStream((cond) ? ::agent::log::Level::Debug : ::agent::log::Level::Off, module)

}  // namespace agent::log
