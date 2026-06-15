#include "script_tools.h"
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
#include <fstream>
#include <filesystem>
#include <random>

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
namespace fs = std::filesystem;

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

std::string random_suffix() {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(100000, 999999);
    return std::to_string(dist(rng));
}

std::string write_temp_file(const std::string& content, const std::string& ext) {
    fs::path temp_dir = fs::temp_directory_path();
    fs::path temp_file = temp_dir / ("agent_script_" + random_suffix() + ext);
    std::ofstream out(temp_file, std::ios::binary);
    if (!out.is_open()) {
        return "";
    }
    out.write(content.data(), content.size());
    out.close();
    return temp_file.string();
}

struct ScriptResult {
    std::string output;
    int exit_code = -1;
    bool timed_out = false;
    std::string error;
};

ScriptResult run_script(const std::string& command, int timeout_seconds) {
    ScriptResult result;

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
        result.error = "Script timed out after " + std::to_string(timeout_seconds) + " seconds";
    }

    while (ReadFile(hStdoutRead, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        output += buffer;
    }
    while (ReadFile(hStderrRead, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        output += buffer;
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
        result.error = "Failed to create pipe for script execution";
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
        result.error = "Script timed out after " + std::to_string(timeout_seconds) + " seconds";
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

void cleanup_file(const std::string& path) {
    if (!path.empty()) {
        std::error_code ec;
        fs::remove(path, ec);
    }
}

} // anonymous namespace

// ============================================================
// PythonScriptTool
// ============================================================

u8str PythonScriptTool::name() const { return u8str_util::to_u8str("python_script"); }

u8str PythonScriptTool::description() const {
    return u8str_util::to_u8str("Write and execute a Python script. The script is saved to a temporary file, executed, and the output is returned. The temporary file is automatically cleaned up after execution.");
}

u8str PythonScriptTool::parameters_schema() const {
    return u8str_util::to_u8str(R"schema({
        "type": "object",
        "properties": {
            "script": {"type": "string", "description": "The Python script code to execute"},
            "timeout_seconds": {"type": "integer", "description": "Timeout in seconds, default 30", "default": 30}
        },
        "required": ["script"]
    })schema");
}

u8str PythonScriptTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto script_it = args.find("script");
    if (script_it == args.end() || !script_it->is_string()) {
        json err;
        err["success"] = false;
        err["error"] = "Missing required parameter: script";
        return u8str_util::to_u8str(safe_dump(err));
    }

    int timeout = 30;
    auto timeout_it = args.find("timeout_seconds");
    if (timeout_it != args.end() && timeout_it->is_number_integer()) {
        timeout = timeout_it->get<int>();
        if (timeout < 1) timeout = 1;
        if (timeout > 300) timeout = 300;
    }

    std::string script_path = write_temp_file(script_it->get<std::string>(), ".py");
    if (script_path.empty()) {
        json err;
        err["success"] = false;
        err["error"] = "Failed to write temporary script file";
        return u8str_util::to_u8str(safe_dump(err));
    }

#ifdef _WIN32
    std::string cmd = "python \"" + script_path + "\"";
#else
    std::string cmd = "python3 \"" + script_path + "\"";
#endif

    auto script_result = run_script(cmd, timeout);
    cleanup_file(script_path);

    // 对脚本输出进行 UTF-8 清洗，防止 GBK 等非 UTF-8 字节导致 JSON 序列化失败
    std::string safe_output = llm::sanitize_utf8_string(script_result.output);

    json result;
    if (!script_result.error.empty() && script_result.output.empty()) {
        result["success"] = false;
        result["error"] = script_result.error;
    } else {
        result["success"] = true;
    }
    result["stdout"] = safe_output;
    result["exit_code"] = script_result.exit_code;
    result["timed_out"] = script_result.timed_out;
    if (!script_result.error.empty()) {
        result["error"] = llm::sanitize_utf8_string(script_result.error);
    }

    return u8str_util::to_u8str(safe_dump(result));
}

void PythonScriptTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool PythonScriptTool::requires_confirmation() const { return false; }

// ============================================================
// ShellScriptTool
// ============================================================

u8str ShellScriptTool::name() const { return u8str_util::to_u8str("run_script"); }

u8str ShellScriptTool::description() const {
#ifdef _WIN32
    return u8str_util::to_u8str("Write and execute a Windows batch script (.bat). The script is saved to a temporary file, executed by cmd.exe, and the output is returned. The temporary file is automatically cleaned up after execution.");
#else
    return u8str_util::to_u8str("Write and execute a Shell script (.sh). The script is saved to a temporary file, executed by /bin/sh, and the output is returned. The temporary file is automatically cleaned up after execution.");
#endif
}

u8str ShellScriptTool::parameters_schema() const {
    return u8str_util::to_u8str(R"schema({
        "type": "object",
        "properties": {
            "script": {"type": "string", "description": "The script code to execute"},
            "timeout_seconds": {"type": "integer", "description": "Timeout in seconds, default 30", "default": 30}
        },
        "required": ["script"]
    })schema");
}

u8str ShellScriptTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto script_it = args.find("script");
    if (script_it == args.end() || !script_it->is_string()) {
        json err;
        err["success"] = false;
        err["error"] = "Missing required parameter: script";
        return u8str_util::to_u8str(safe_dump(err));
    }

    int timeout = 30;
    auto timeout_it = args.find("timeout_seconds");
    if (timeout_it != args.end() && timeout_it->is_number_integer()) {
        timeout = timeout_it->get<int>();
        if (timeout < 1) timeout = 1;
        if (timeout > 300) timeout = 300;
    }

    std::string script_content = script_it->get<std::string>();
#ifdef _WIN32
    std::string script_path = write_temp_file(script_content, ".bat");
    if (script_path.empty()) {
        json err;
        err["success"] = false;
        err["error"] = "Failed to write temporary script file";
        return u8str_util::to_u8str(safe_dump(err));
    }
    std::string cmd = "\"" + script_path + "\"";
#else
    std::string script_path = write_temp_file(script_content, ".sh");
    if (script_path.empty()) {
        json err;
        err["success"] = false;
        err["error"] = "Failed to write temporary script file";
        return u8str_util::to_u8str(safe_dump(err));
    }
    fs::permissions(script_path, fs::perms::owner_exec, fs::perm_options::add);
    std::string cmd = "sh \"" + script_path + "\"";
#endif

    auto script_result = run_script(cmd, timeout);
    cleanup_file(script_path);

    // 对脚本输出进行 UTF-8 清洗，防止 GBK 等非 UTF-8 字节导致 JSON 序列化失败
    std::string safe_output = llm::sanitize_utf8_string(script_result.output);

    json result;
    if (!script_result.error.empty() && script_result.output.empty()) {
        result["success"] = false;
        result["error"] = script_result.error;
    } else {
        result["success"] = true;
    }
    result["stdout"] = safe_output;
    result["exit_code"] = script_result.exit_code;
    result["timed_out"] = script_result.timed_out;
    if (!script_result.error.empty()) {
        result["error"] = llm::sanitize_utf8_string(script_result.error);
    }

    return u8str_util::to_u8str(safe_dump(result));
}

void ShellScriptTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool ShellScriptTool::requires_confirmation() const { return false; }

ToolPtr create_python_script_tool() {
    return std::make_shared<PythonScriptTool>();
}

ToolPtr create_shell_script_tool() {
    return std::make_shared<ShellScriptTool>();
}

} // namespace agent