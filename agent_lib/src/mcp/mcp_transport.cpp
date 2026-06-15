#include "mcp_transport.h"
#include <util/i_http_client.h>
#include <util/log.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef AGENT_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#endif

namespace agent {
namespace mcp {

namespace {

std::string read_line_from_pipe(HANDLE hPipe) {
    std::string line;
    char ch;
    DWORD bytesRead;
    while (true) {
        if (!ReadFile(hPipe, &ch, 1, &bytesRead, nullptr)) {
            break;
        }
        if (bytesRead == 0) break;
        if (ch == '\n') break;
        if (ch != '\r') {
            line += ch;
        }
    }
    return line;
}

bool write_to_pipe(HANDLE hPipe, const std::string& data) {
    DWORD bytesWritten;
    return WriteFile(hPipe, data.c_str(), static_cast<DWORD>(data.size()), &bytesWritten, nullptr);
}

class StdioTransport : public ITransport {
public:
    StdioTransport(std::string command, std::vector<std::string> args,
                   std::map<std::string, std::string> env, int timeout_ms)
        : command_(std::move(command))
        , args_(std::move(args))
        , env_(std::move(env))
        , timeout_ms_(timeout_ms) {}

    ~StdioTransport() override {
        disconnect();
    }

    bool connect() override {
        if (connected_) return true;

        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = nullptr;

        if (!CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &saAttr, 0)) {
            return false;
        }
        if (!SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0)) {
            close_pipe(hChildStdoutRd);
            close_pipe(hChildStdoutWr);
            return false;
        }

        if (!CreatePipe(&hChildStdinRd, &hChildStdinWr, &saAttr, 0)) {
            close_pipe(hChildStdoutRd);
            close_pipe(hChildStdoutWr);
            return false;
        }
        if (!SetHandleInformation(hChildStdinWr, HANDLE_FLAG_INHERIT, 0)) {
            close_pipe(hChildStdoutRd);
            close_pipe(hChildStdoutWr);
            close_pipe(hChildStdinRd);
            close_pipe(hChildStdinWr);
            return false;
        }

        std::string cmd_line = build_command_line();
        std::string env_block = build_env_block();

        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdError = hChildStdoutWr;
        si.hStdOutput = hChildStdoutWr;
        si.hStdInput = hChildStdinRd;
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        BOOL success = CreateProcessA(
            nullptr,
            &cmd_line[0],
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            env_block.empty() ? nullptr : &env_block[0],
            nullptr,
            &si,
            &pi
        );

        if (!success) {
            close_pipe(hChildStdoutRd);
            close_pipe(hChildStdoutWr);
            close_pipe(hChildStdinRd);
            close_pipe(hChildStdinWr);
            return false;
        }

        piProcInfo = pi;
        CloseHandle(pi.hThread);

        close_pipe(hChildStdoutWr);
        close_pipe(hChildStdinRd);

        connected_ = true;
        return true;
    }

    void disconnect() override {
        if (!connected_) return;
        connected_ = false;

        if (piProcInfo.hProcess) {
            TerminateProcess(piProcInfo.hProcess, 0);
            WaitForSingleObject(piProcInfo.hProcess, 5000);
            CloseHandle(piProcInfo.hProcess);
            piProcInfo.hProcess = nullptr;
        }

        close_pipe(hChildStdoutRd);
        close_pipe(hChildStdinWr);
    }

    bool is_connected() const override {
        return connected_;
    }

    nlohmann::json send_request(const nlohmann::json& request, int timeout_ms) override {
        if (!connected_) {
            throw std::runtime_error("StdioTransport: not connected");
        }

        std::string req_str = request.dump() + "\n";
        if (!write_to_pipe(hChildStdinWr, req_str)) {
            disconnect();
            throw std::runtime_error("StdioTransport: failed to write to stdin");
        }

        auto start_time = std::chrono::steady_clock::now();

        while (true) {
            DWORD bytesAvail = 0;
            if (!PeekNamedPipe(hChildStdoutRd, nullptr, 0, nullptr, &bytesAvail, nullptr)) {
                disconnect();
                throw std::runtime_error("StdioTransport: pipe broken");
            }

            if (bytesAvail > 0) {
                std::string line = read_line_from_pipe(hChildStdoutRd);
                if (!line.empty()) {
                    // 跳过非 JSON 行（子进程的日志、启动信息等）
                    // JSON-RPC 响应以 '{' 开头
                    size_t first_non_space = line.find_first_not_of(" \t\r\n");
                    if (first_non_space != std::string::npos && line[first_non_space] == '{') {
                        try {
                            auto json_resp = nlohmann::json::parse(line);
                            return json_resp;
                        } catch (const std::exception&) {
                            // JSON 解析失败，继续读取下一行
                        }
                    }
                    // 非 JSON 行或解析失败，继续循环读取
                }
            }

            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms) {
                disconnect();
                throw std::runtime_error("StdioTransport: timeout waiting for response");
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void send_notification(const nlohmann::json& notification, int timeout_ms) override {
        if (!connected_) {
            throw std::runtime_error("StdioTransport: not connected");
        }

        std::string req_str = notification.dump() + "\n";
        if (!write_to_pipe(hChildStdinWr, req_str)) {
            disconnect();
            throw std::runtime_error("StdioTransport: failed to write notification to stdin");
        }
    }

    std::string name() const override {
        return "stdio:" + command_;
    }

private:
    std::string command_;
    std::vector<std::string> args_;
    std::map<std::string, std::string> env_;
    int timeout_ms_ = 30000;
    bool connected_ = false;

    HANDLE hChildStdoutRd = INVALID_HANDLE_VALUE;
    HANDLE hChildStdoutWr = INVALID_HANDLE_VALUE;
    HANDLE hChildStdinRd = INVALID_HANDLE_VALUE;
    HANDLE hChildStdinWr = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION piProcInfo{};

    static void close_pipe(HANDLE& h) {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) {
            CloseHandle(h);
            h = INVALID_HANDLE_VALUE;
        }
    }

    std::string build_command_line() const {
        std::string cmd = command_;
        for (const auto& arg : args_) {
            cmd += " \"" + arg + "\"";
        }
        return cmd;
    }

    std::string build_env_block() const {
        if (env_.empty()) return {};

        LPCH env_block = GetEnvironmentStrings();
        if (!env_block) {
            return {};
        }

        std::map<std::string, std::string> merged_env = env_;

        LPCH env_ptr = env_block;
        while (*env_ptr) {
            std::string entry(env_ptr);
            auto eq_pos = entry.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = entry.substr(0, eq_pos);
                if (merged_env.find(key) == merged_env.end()) {
                    merged_env[key] = entry.substr(eq_pos + 1);
                }
            }
            env_ptr += entry.size() + 1;
        }

        FreeEnvironmentStrings(env_block);

        std::string block;
        for (const auto& [key, value] : merged_env) {
            block += key + "=" + value + '\0';
        }
        block += '\0';
        return block;
    }
};

class HttpTransport : public ITransport {
public:
    HttpTransport(std::string url, int timeout_ms)
        : url_(std::move(url))
        , timeout_ms_(timeout_ms) {}

    bool connect() override {
        if (connected_) return true;

        auto client = create_http_client();
        if (!client) {
            return false;
        }

        std::map<std::string, std::string> headers;
        auto resp = client->get(url_, headers, timeout_ms_);

        if (resp.is_error) {
            AGENT_LOG_ERROR("HttpTransport") << "Failed to connect to " << url_
                      << ": " << resp.error_message;
            return false;
        }

        connected_ = true;
        return true;
    }

    void disconnect() override {
        connected_ = false;
    }

    bool is_connected() const override {
        return connected_;
    }

    nlohmann::json send_request(const nlohmann::json& request, int timeout_ms) override {
        if (!connected_) {
            throw std::runtime_error("HttpTransport: not connected");
        }

        auto client = create_http_client();
        if (!client) {
            throw std::runtime_error("HttpTransport: failed to create HTTP client");
        }

        int actual_timeout = (timeout_ms > 0) ? timeout_ms : timeout_ms_;
        std::string body = request.dump();

        std::map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";
        headers["Accept"] = "application/json, text/event-stream";

        auto resp = client->post(url_, body, headers, actual_timeout);

        if (resp.is_error) {
            throw std::runtime_error("HttpTransport: request failed - " + resp.error_message);
        }

        if (resp.body.empty()) {
            throw std::runtime_error("HttpTransport: empty response");
        }

        try {
            return nlohmann::json::parse(resp.body);
        } catch (const std::exception& e) {
            throw std::runtime_error("HttpTransport: failed to parse JSON response: " + std::string(e.what()));
        }
    }

    std::string name() const override {
        return "http:" + url_;
    }

    void send_notification(const nlohmann::json& notification, int timeout_ms) override {
        if (!connected_) {
            throw std::runtime_error("HttpTransport: not connected");
        }

        auto client = create_http_client();
        if (!client) {
            throw std::runtime_error("HttpTransport: failed to create HTTP client");
        }

        int actual_timeout = (timeout_ms > 0) ? timeout_ms : timeout_ms_;
        std::string body = notification.dump();

        std::map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";

        auto resp = client->post(url_, body, headers, actual_timeout);
        if (resp.is_error) {
            throw std::runtime_error("HttpTransport: notification failed - " + resp.error_message);
        }
    }

private:
    std::string url_;
    int timeout_ms_ = 30000;
    bool connected_ = false;
};

} // anonymous namespace

TransportPtr create_stdio_transport(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::map<std::string, std::string>& env,
    int timeout_ms) {
    return std::make_unique<StdioTransport>(command, args, env, timeout_ms);
}

TransportPtr create_http_transport(
    const std::string& url,
    int timeout_ms) {
    return std::make_unique<HttpTransport>(url, timeout_ms);
}

} // namespace mcp
} // namespace agent