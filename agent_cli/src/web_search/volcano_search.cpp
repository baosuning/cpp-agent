#include "volcano_search.h"
#include "web_search_common.h"
#include <util/log.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace agent {

namespace {

using json = nlohmann::json;

std::vector<WebSearchResult> search_volcano(const std::string& query, int count, const std::string& api_key) {
    if (api_key.empty()) {
        AGENT_LOG_WARN("Volcano") << "API key is empty";
        return {};
    }

    std::vector<WebSearchResult> results;
#ifdef _WIN32
    HANDLE hStdInRd = NULL, hStdInWr = NULL, hStdOutRd = NULL, hStdOutWr = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

    if (!CreatePipe(&hStdInRd, &hStdInWr, &sa, 0)) return {};
    if (!CreatePipe(&hStdOutRd, &hStdOutWr, &sa, 0)) {
        CloseHandle(hStdInRd); CloseHandle(hStdInWr);
        return {};
    }

    SetHandleInformation(hStdInWr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdOutRd, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi = {0};
    STARTUPINFOA si = {0};
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdInRd;
    si.hStdOutput = hStdOutWr;
    si.hStdError = hStdOutWr;  // stderr 重定向到 stdout 管道，避免继承父进程的 stderr 句柄

    std::string cmd = "uvx --from git+https://github.com/volcengine/mcp-server#subdirectory=server/mcp_server_askecho_search_infinity mcp-server-askecho-search-infinity";
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back('\0');

    std::string env_block;
    {
        LPCH cur_env = GetEnvironmentStrings();
        if (cur_env) {
            LPCH p = cur_env;
            while (*p) {
                std::string entry(p);
                if (entry.find("ASK_ECHO_SEARCH_INFINITY_API_KEY=") != 0) {
                    env_block += entry + '\0';
                }
                p += entry.size() + 1;
            }
            FreeEnvironmentStrings(cur_env);
        }
        env_block += "ASK_ECHO_SEARCH_INFINITY_API_KEY=" + api_key + '\0';
        env_block += '\0';
    }

    if (!CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, (LPVOID)env_block.data(), NULL, &si, &pi)) {
        CloseHandle(hStdInRd); CloseHandle(hStdInWr);
        CloseHandle(hStdOutRd); CloseHandle(hStdOutWr);
        return {};
    }

    CloseHandle(hStdInRd);
    CloseHandle(hStdOutWr);

    auto read_line_with_timeout = [](HANDLE h, DWORD timeout_ms) -> std::string {
        DWORD total_waited = 0;
        const DWORD CHECK_INTERVAL = 100;
        std::string line;
        while (total_waited < timeout_ms) {
            DWORD bytes_avail = 0;
            if (PeekNamedPipe(h, NULL, 0, NULL, &bytes_avail, NULL) && bytes_avail > 0) {
                char c;
                DWORD read;
                while (ReadFile(h, &c, 1, &read, NULL) && read == 1) {
                    if (c == '\n') return line;
                    if (c != '\r') line += c;
                }
                return line;
            }
            Sleep(CHECK_INTERVAL);
            total_waited += CHECK_INTERVAL;
        }
        return line;
    };

    DWORD written;

    auto send_msg = [&](const std::string& msg) {
        std::string full = msg + "\n";
        WriteFile(hStdInWr, full.c_str(), static_cast<DWORD>(full.size()), &written, NULL);
    };

    send_msg(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"agent-framework","version":"1.0"}}})");

    std::string init_resp = read_line_with_timeout(hStdOutRd, 120000);
    if (init_resp.empty()) {
        CloseHandle(hStdInWr);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        CloseHandle(hStdOutRd);
        return {};
    }

    send_msg(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");

    send_msg(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");

    std::string tools_resp = read_line_with_timeout(hStdOutRd, 30000);
    if (tools_resp.empty()) {
        CloseHandle(hStdInWr);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        CloseHandle(hStdOutRd);
        return {};
    }

    std::string tool_name = "web_search";
    try {
        auto tools_json = json::parse(tools_resp);
        if (tools_json.contains("result") && tools_json["result"].contains("tools") &&
            tools_json["result"]["tools"].is_array() && !tools_json["result"]["tools"].empty()) {
            tool_name = tools_json["result"]["tools"][0]["name"].get<std::string>();
        }
    } catch (...) {
        AGENT_LOG_DEBUG("Volcano") << "Failed to parse tools/list response, using default tool name";
    }

    json call_args;
    call_args["Query"] = query;
    call_args["Count"] = count;

    json call_req;
    call_req["jsonrpc"] = "2.0";
    call_req["id"] = 3;
    call_req["method"] = "tools/call";
    call_req["params"]["name"] = tool_name;
    call_req["params"]["arguments"] = call_args;

    send_msg(call_req.dump());

    std::string call_resp = read_line_with_timeout(hStdOutRd, 60000);

    if (!call_resp.empty()) {
        try {
            auto resp_json = json::parse(call_resp);
            if (resp_json.contains("result") && resp_json["result"].contains("content") &&
                resp_json["result"]["content"].is_array()) {
                for (const auto& item : resp_json["result"]["content"]) {
                    if (item.contains("type") && item["type"] == "text" && item.contains("text")) {
                        std::string text = item["text"].get<std::string>();
                        WebSearchResult entry;
                        entry.title = "Search Results for: " + query;
                        entry.url = "";
                        entry.snippet = text;
                        results.push_back(std::move(entry));
                    }
                }
            }
        } catch (...) {
        AGENT_LOG_DEBUG("Volcano") << "Failed to parse search response";
    }
    }

    CloseHandle(hStdInWr);
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hStdOutRd);
#endif
    return results;
}

} // anonymous namespace

VolcanoSearch::VolcanoSearch(std::string api_key)
    : api_key_(std::move(api_key)) {}

u8str VolcanoSearch::name() const {
    return get_tool_name();
}

u8str VolcanoSearch::description() const {
    return get_tool_description();
}

u8str VolcanoSearch::get_tool_name() const {
    return u8str(u8"volcano_search");
}

u8str VolcanoSearch::get_tool_description() const {
    return u8str(u8"Search the web using Volcano Engine MCP Server. Requires ASK_ECHO_SEARCH_INFINITY_API_KEY. Returns search results via standalone web search service.");
}

u8str VolcanoSearch::parameters_schema() const {
    return to_u8str(R"schema({
        "type": "object",
        "properties": {
            "query": {"type": "string", "description": "The search query"},
            "count": {"type": "integer", "description": "Number of results to return, max 20", "default": 5}
        },
        "required": ["query"]
    })schema");
}

std::vector<WebSearchResult> VolcanoSearch::do_search(const std::string& query, int count) {
    return search_volcano(query, count, api_key_);
}

} // namespace agent