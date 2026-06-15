#include "execute_command_tool.h"
#include "../util/utf8_utils.h"
#include <util/u8str_utils.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <memory>
#include <future>
#include <chrono>
#include <string>
#include <thread>
#include <atomic>

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#include <handleapi.h>
#include <synchapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

namespace agent {

namespace {

using json = nlohmann::json;

// 安全的 JSON 序列化，遇到无效 UTF-8 字节时用 U+FFFD 替换而非抛出异常
static std::string safe_dump(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

json parse_args(const u8str& arguments) {
    try {
        return json::parse(u8str_util::to_string(llm::sanitize_utf8(arguments)));
    } catch (...) {
        return json::object();
    }
}

struct CommandResult {
    std::string  output;
    int          exit_code = -1;
    bool         timed_out = false;
    std::string  error;
};

CommandResult run_command(const std::string& command, int timeout_seconds) {
    CommandResult result;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

    HANDLE hStdoutRead, hStdoutWrite;
    HANDLE hStderrRead, hStderrWrite;

    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        result.error = "Failed to create stdout pipe";
        return result;
    }

    if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        result.error = "Failed to create stderr pipe";
        return result;
    }

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {0};

    std::string full_cmd = "cmd /c " + command;
    if (!CreateProcessA(NULL, const_cast<char*>(full_cmd.c_str()), NULL, NULL, TRUE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        CloseHandle(hStderrRead);
        CloseHandle(hStderrWrite);
        result.error = "Failed to create process: " + std::to_string(GetLastError());
        return result;
    }

    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout_seconds * 1000);

    char buffer[8192];
    std::string output;
    DWORD bytes_read;

    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        result.timed_out = true;
        result.error = "Command timed out after " + std::to_string(timeout_seconds) + " seconds";
    }

    while (ReadFile(hStdoutRead, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        output += buffer;
    }
    while (ReadFile(hStderrRead, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        output += buffer;
    }

    if (!output.empty()) {
        int codepage = GetOEMCP();
        if (codepage <= 0 || codepage == CP_UTF8) {
            codepage = 936;
        }

        int wlen = MultiByteToWideChar(codepage, 0, output.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wstr(wlen - 1, L'\0');
            MultiByteToWideChar(codepage, 0, output.c_str(), -1, &wstr[0], wlen);
            int utf8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8len > 0) {
                std::string utf8out(utf8len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8out[0], utf8len, nullptr, nullptr);
                output = utf8out;
            }
        }
    }

    if (!result.timed_out) {
        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        result.exit_code = exit_code;
    }
    result.output = output;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);

#else
    std::string full_cmd = command + " 2>&1";
    FILE* pipe = popen(full_cmd.c_str(), "r");

    if (!pipe) {
        result.error = "Failed to create pipe for command execution";
        return result;
    }

    std::atomic<bool> read_done{false};
    std::string output;

    auto read_future = std::async(std::launch::async, [&]() {
        char buffer[8192];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }
        read_done = true;
        return output;
    });

    auto timeout_future = std::async(std::launch::async, [&]() {
        std::this_thread::sleep_for(std::chrono::seconds(timeout_seconds));
        return !read_done.load();
    });

    if (timeout_future.get()) {
        result.timed_out = true;
        result.error = "Command timed out after " + std::to_string(timeout_seconds) + " seconds";
        result.output = output;
    } else {
        result.output = read_future.get();
        result.exit_code = pclose(pipe);
#ifdef WIFEXITED
        if (WIFEXITED(result.exit_code)) {
            result.exit_code = WEXITSTATUS(result.exit_code);
        }
#endif
    }
#endif

    return result;
}

} // anonymous namespace

u8str ExecuteCommandTool::name() const { return u8str_util::to_u8str("execute_command"); }

u8str ExecuteCommandTool::description() const {
    return u8str_util::to_u8str("Execute a terminal command and return its output. Supports a configurable timeout.");
}

u8str ExecuteCommandTool::parameters_schema() const {
    return u8str_util::to_u8str(R"schema({
        "type": "object",
        "properties": {
            "command": {"type": "string", "description": "The command to execute"},
            "timeout_seconds": {"type": "integer", "description": "Timeout in seconds, default 30", "default": 30}
        },
        "required": ["command"]
    })schema");
}

u8str ExecuteCommandTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto cmd_it = args.find("command");
    if (cmd_it == args.end() || !cmd_it->is_string()) {
        json err;
        err["success"] = false;
        err["error"] = "Missing required parameter: command";
        return u8str_util::to_u8str(safe_dump(err));
    }

    int timeout = 30;
    auto timeout_it = args.find("timeout_seconds");
    if (timeout_it != args.end() && timeout_it->is_number_integer()) {
        timeout = timeout_it->get<int>();
        if (timeout < 1) timeout = 1;
        if (timeout > 300) timeout = 300;
    }

    auto cmd_result = run_command(cmd_it->get<std::string>(), timeout);

    // 对命令输出进行 UTF-8 清洗，防止 GBK 等非 UTF-8 字节导致 JSON 序列化失败
    std::string safe_output = llm::sanitize_utf8_string(cmd_result.output);

    json result;
    if (!cmd_result.error.empty() && cmd_result.output.empty()) {
        result["success"] = false;
        result["error"] = cmd_result.error;
    } else {
        result["success"] = true;
    }
    result["stdout"] = safe_output;
    result["exit_code"] = cmd_result.exit_code;
    result["timed_out"] = cmd_result.timed_out;
    if (!cmd_result.error.empty()) {
        result["error"] = llm::sanitize_utf8_string(cmd_result.error);
    }
    result["command"] = cmd_it->get<std::string>();

    return u8str_util::to_u8str(safe_dump(result));
}

void ExecuteCommandTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool ExecuteCommandTool::requires_confirmation() const { return false; }

ToolPtr create_execute_command_tool() {
    return std::make_shared<ExecuteCommandTool>();
}

} // namespace agent