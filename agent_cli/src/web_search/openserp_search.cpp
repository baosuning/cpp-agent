#include "openserp_search.h"
#include "web_search_common.h"
#include <util/i_http_client.h>
#include <util/log.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <mutex>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

namespace agent {

namespace {

using json = nlohmann::json;

class OpenSerpServerManager {
public:
    static OpenSerpServerManager& instance() {
        static OpenSerpServerManager mgr;
        return mgr;
    }

    bool start(int port = 7070) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (process_handle_) return true;

        std::string exe_path = resolve_executable_path();
        AGENT_LOG_DEBUG("OpenSerp") << "Resolved exe path: " << exe_path;

        // Check if exe exists
        DWORD attr = GetFileAttributesA(exe_path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) {
            char cwd_buf[MAX_PATH];
            std::string cwd_str = "N/A";
            if (GetCurrentDirectoryA(MAX_PATH, cwd_buf)) {
                cwd_str = cwd_buf;
            }
            AGENT_LOG_ERROR("OpenSerp") << "Executable not found at: " << exe_path << " (CWD: " << cwd_str << ")";
        } else {
            AGENT_LOG_DEBUG("OpenSerp") << "Executable exists, attributes: " << attr;
        }

        std::string cmd = "\"" + exe_path + "\" serve -a 127.0.0.1 -p " + std::to_string(port) + " --timeout=20";
        AGENT_LOG_DEBUG("OpenSerp") << "Starting: " << cmd;
        std::vector<char> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back('\0');

        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
        PROCESS_INFORMATION pi = {0};
        STARTUPINFOA si = {0};
        si.cb = sizeof(STARTUPINFOA);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        if (!CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, TRUE,
                            CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, NULL, NULL, &si, &pi)) {
            DWORD err = GetLastError();
            AGENT_LOG_ERROR("OpenSerp") << "Failed to start process, error: " << err;
            return false;
        }

        AGENT_LOG_DEBUG("OpenSerp") << "Process started, PID: " << pi.dwProcessId;
        process_handle_ = pi.hProcess;
        thread_handle_ = pi.hThread;
        port_ = port;

        if (!wait_for_ready(port, 20000)) {
            AGENT_LOG_ERROR("OpenSerp") << "Server failed to become ready within 20s";
            stop();
            return false;
        }

        return true;
    }

    void stop() {
        if (process_handle_) {
            TerminateProcess(process_handle_, 0);
            WaitForSingleObject(process_handle_, 5000);
            CloseHandle(process_handle_);
            process_handle_ = NULL;
        }
        if (thread_handle_) {
            CloseHandle(thread_handle_);
            thread_handle_ = NULL;
        }
    }

    int port() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return port_;
    }

    bool is_running() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return process_handle_ != NULL;
    }

    ~OpenSerpServerManager() { stop(); }

private:
    mutable std::mutex mutex_;
    HANDLE process_handle_ = NULL;
    HANDLE thread_handle_ = NULL;
    int port_ = 7070;

    static std::string resolve_executable_path() {
        const char* env_path = std::getenv("OPENSERP_PATH");
        if (env_path && *env_path) return env_path;

        return "openserp.exe";
    }

    static bool wait_for_ready(int port, int timeout_ms) {
        AGENT_LOG_DEBUG("OpenSerp") << "Waiting for server on port " << port << "...";
        auto client = create_http_client();
        if (!client) {
            AGENT_LOG_ERROR("OpenSerp") << "Failed to create HTTP client for health check";
            return false;
        }
        int waited = 0;
        const int interval = 200;
        while (waited < timeout_ms) {
            std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
            std::map<std::string, std::string> headers = {
                {"User-Agent", "Agent-Framework/1.0"}
            };
            auto response = client->get(url, headers, 5000);
            if (!response.is_error) {
                AGENT_LOG_DEBUG("OpenSerp") << "Server ready! (HTTP " << response.status_code
                          << ", body length: " << response.body.size() << ")";
                return true;
            }
            if (waited == 0) {
                AGENT_LOG_DEBUG("OpenSerp") << "Health check failed: " << response.error_message
                          << " (retrying every " << interval << "ms)";
            }
            Sleep(interval);
            waited += interval;
        }
        AGENT_LOG_WARN("OpenSerp") << "Server not ready after " << timeout_ms << "ms timeout";
        return false;
    }
};

std::vector<WebSearchResult> search_openserp(const std::string& query, int count,
                                              const std::vector<std::string>& engines,
                                              int port) {
    AGENT_LOG_DEBUG("OpenSerp") << "Searching for: " << query << " on port " << port;
    std::vector<WebSearchResult> all_results;
    std::set<std::string> seen_urls;

    for (const auto& engine : engines) {
        std::string engine_lower = engine;
        std::transform(engine_lower.begin(), engine_lower.end(), engine_lower.begin(), ::tolower);

        std::string path = "/" + engine_lower + "/search?text=" + url_encode(query)
                         + "&limit=" + std::to_string(count) + "&format=json";

        AGENT_LOG_DEBUG("OpenSerp") << "Query path: " << path;

        std::vector<std::pair<std::string, std::string>> headers = {
            {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"},
            {"Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8"}
        };
        
        AGENT_LOG_DEBUG("OpenSerp") << "Sending HTTP request...";
        std::string response = winhttp_get_with_headers("127.0.0.1", path, false, headers, 30, port);
        AGENT_LOG_DEBUG("OpenSerp") << "HTTP request returned, response size: " << response.size();

        if (response.empty()) {
            AGENT_LOG_ERROR("OpenSerp") << "Empty response for engine: " << engine_lower;
            continue;
        }

        AGENT_LOG_DEBUG("OpenSerp") << "Response length: " << response.length() << " chars";
        if (response.length() < 500) {
            AGENT_LOG_DEBUG("OpenSerp") << "Response content: " << response;
        }

        try {
            auto json_resp = nlohmann::json::parse(response);
            if (json_resp.contains("results") && json_resp["results"].is_array()) {
                for (const auto& item : json_resp["results"]) {
                    if (item.contains("type") && item["type"] == "organic") {
                        std::string url = item.contains("url") ? item["url"].get<std::string>() : "";
                        if (url.empty() || seen_urls.count(url)) continue;
                        seen_urls.insert(url);

                        WebSearchResult entry;
                        entry.title = item.contains("title") ? item["title"].get<std::string>() : "";
                        entry.url = url;
                        entry.snippet = item.contains("snippet") ? item["snippet"].get<std::string>() : "";
                        all_results.push_back(std::move(entry));

                        if ((int)all_results.size() >= count) break;
                    }
                }
            }
        } catch (const std::exception& e) {
            AGENT_LOG_ERROR("OpenSerp") << "Parse error: " << e.what();
        } catch (...) {
            AGENT_LOG_ERROR("OpenSerp") << "Unknown parse error";
        }

        if ((int)all_results.size() >= count) break;
    }

    return all_results;
}

} // anonymous namespace

OpenSerpSearch::OpenSerpSearch(std::vector<std::string> engines)
    : engines_(std::move(engines)) {
    if (engines_.empty()) {
        engines_.push_back("Bing");
    }
}

u8str OpenSerpSearch::name() const {
    return get_tool_name();
}

u8str OpenSerpSearch::description() const {
    return get_tool_description();
}

u8str OpenSerpSearch::get_tool_name() const {
    return u8str(u8"web_search");
}

u8str OpenSerpSearch::get_tool_description() const {
    std::string desc = "Search the web using self-hosted OpenSERP API. ";
    desc += "Automatically starts openserp.exe as a subprocess. ";
    desc += "Configured engines: ";
    for (size_t i = 0; i < engines_.size(); i++) {
        if (i > 0) desc += ", ";
        desc += engines_[i];
    }
    desc += ". Returns title, url and snippet for each result.";
    return to_u8str(desc);
}

u8str OpenSerpSearch::parameters_schema() const {
    return to_u8str(R"schema({
        "type": "object",
        "properties": {
            "query": {"type": "string", "description": "The search query"},
            "count": {"type": "integer", "description": "Number of results to return, max 20", "default": 5}
        },
        "required": ["query"]
    })schema");
}

std::vector<WebSearchResult> OpenSerpSearch::do_search(const std::string& query, int count) {
    auto& mgr = OpenSerpServerManager::instance();
    if (!mgr.is_running()) {
        const char* port_str = std::getenv("OPENSERP_PORT");
        int port = 7070;
        if (port_str && *port_str) port = std::atoi(port_str);
        if (!mgr.start(port)) {
            return {};  // 启动失败，返回空结果
        }
    }
    return search_openserp(query, count, engines_, mgr.port());
}

} // namespace agent