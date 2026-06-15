// agent_lib/src/agent/log.cpp
// 统一日志模块实现

#include <util/log.h>
#include <chrono>
#include <ctime>
#include <iomanip>

namespace agent::log {

namespace {
Level g_level = Level::Info;
bool  g_to_stderr = false;  // 默认输出到 stdout（兼容性）

const char* level_str(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
        default:           return "OFF";
    }
}
}  // namespace

std::mutex LogStream::mutex_;

void set_level(Level level) {
    g_level = level;
}

Level get_level() {
    return g_level;
}

void set_output_to_stderr(bool enable) {
    g_to_stderr = enable;
}

bool is_output_to_stderr() {
    return g_to_stderr;
}

LogStream::LogStream(Level level, const char* module)
    : enabled_(level != Level::Off && static_cast<int>(level) >= static_cast<int>(g_level))
    , level_(level)
    , module_(module) {
    if (enabled_) {
        // 时间戳
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
        stream_ << "[" << std::put_time(&tm_buf, "%H:%M:%S")
                << "." << std::setfill('0') << std::setw(3) << ms.count() << "]"
                << "[" << level_str(level_) << "]"
                << "[" << (module_ ? module_ : "?") << "] ";
    }
}

LogStream::~LogStream() {
    if (enabled_) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& out = g_to_stderr ? std::cerr : std::cout;
        out << stream_.str() << std::endl;
    }
}

}  // namespace agent::log
