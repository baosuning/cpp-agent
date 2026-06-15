#include "web_fetch_tool.h"
#include "../util/utf8_utils.h"
#include <util/u8str_utils.h>
#include <util/i_http_client.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <regex>
#include <sstream>
#include <algorithm>
#include <cstdlib>

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

std::string result_json(bool success, const std::string& data, const std::string& error = "") {
    json j;
    j["success"] = success;
    if (success) {
        j["result"] = data;
    } else {
        j["error"] = error;
    }
    return safe_dump(j);
}

std::string html_to_text(const std::string& html, size_t max_length = 8000) {
    std::string text = html;

    std::regex script_style(R"(<script[^>]*>[\s\S]*?</script>|<style[^>]*>[\s\S]*?</style>|<nav[^>]*>[\s\S]*?</nav>|<footer[^>]*>[\s\S]*?</footer>)",
                            std::regex::icase | std::regex::nosubs);
    text = std::regex_replace(text, script_style, "");

    std::regex comments(R"(<!--[\s\S]*?-->)", std::regex::nosubs);
    text = std::regex_replace(text, comments, "");

    std::regex block_tags(R"(</?(?:div|p|br|h[1-6]|li|tr|td|th|blockquote|section|article|header|main)[^>]*>)",
                          std::regex::icase | std::regex::nosubs);
    text = std::regex_replace(text, block_tags, "\n");

    std::regex tags(R"(<[^>]*>)", std::regex::nosubs);
    text = std::regex_replace(text, tags, "");

    {
        std::string decoded;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t amp = text.find('&', pos);
            if (amp == std::string::npos) {
                decoded += text.substr(pos);
                break;
            }
            decoded += text.substr(pos, amp - pos);
            size_t semi = text.find(';', amp);
            if (semi == std::string::npos) {
                decoded += text.substr(amp);
                break;
            }
            std::string entity = text.substr(amp + 1, semi - amp - 1);
            char ch = ' ';
            if (entity == "amp") ch = '&';
            else if (entity == "lt") ch = '<';
            else if (entity == "gt") ch = '>';
            else if (entity == "quot") ch = '"';
            else if (entity == "apos" || entity == "#39") ch = '\'';
            else if (entity == "nbsp") ch = ' ';
            else if (entity.size() > 1 && entity[0] == '#') {
                try {
                    int code = std::stoi(entity.substr(1));
                    ch = static_cast<char>(code);
                } catch (...) {
                    // Invalid numeric entity code, keep default char
                }
            }
            decoded += ch;
            pos = semi + 1;
        }
        text = decoded;
    }

    std::regex multi_newline(R"(\n{3,})");
    text = std::regex_replace(text, multi_newline, "\n\n");
    std::regex multi_space(R"( {3,})");
    text = std::regex_replace(text, multi_space, "  ");

    std::istringstream stream(text);
    std::string line;
    std::string cleaned;
    while (std::getline(stream, line)) {
        auto start = line.find_first_not_of(" \t\r");
        auto end = line.find_last_not_of(" \t\r");
        if (start != std::string::npos) {
            cleaned += line.substr(start, end - start + 1) + "\n";
        }
    }

    auto trim_start = cleaned.find_first_not_of("\n");
    if (trim_start == std::string::npos) return "";
    auto trim_end = cleaned.find_last_not_of("\n");
    cleaned = cleaned.substr(trim_start, trim_end - trim_start + 1);

    if (cleaned.size() > max_length) {
        cleaned = cleaned.substr(0, max_length) + "\n\n... (truncated)";
    }

    return cleaned;
}

} // anonymous namespace

// ============================================================
// WebFetchTool
// ============================================================

u8str WebFetchTool::name() const { return u8str_util::to_u8str("web_fetch"); }

u8str WebFetchTool::description() const {
    return u8str_util::to_u8str("Fetch and read the content of a web page at the specified URL. Returns the extracted text content of the page.");
}

u8str WebFetchTool::parameters_schema() const {
    return u8str_util::to_u8str(R"schema({
        "type": "object",
        "properties": {
            "url": {"type": "string", "description": "The URL of the web page to fetch, e.g. https://example.com/page"},
            "timeout_seconds": {"type": "integer", "description": "Timeout in seconds, default 30", "default": 30}
        },
        "required": ["url"]
    })schema");
}

u8str WebFetchTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto url_it = args.find("url");
    if (url_it == args.end() || !url_it->is_string()) {
        return u8str_util::to_u8str(result_json(false, "", "Missing required parameter: url"));
    }

    int timeout = 30;
    auto timeout_it = args.find("timeout_seconds");
    if (timeout_it != args.end() && timeout_it->is_number_integer()) {
        timeout = timeout_it->get<int>();
        if (timeout < 1) timeout = 1;
        if (timeout > 120) timeout = 120;
    }

    auto url = url_it->get<std::string>();

    auto client = create_http_client();
    if (!client) {
        return u8str_util::to_u8str(result_json(false, "", "Failed to create HTTP client"));
    }

    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

    auto response = client->get(url, headers, timeout * 1000);

    if (response.is_error || response.body.empty()) {
        return u8str_util::to_u8str(result_json(false, "", "Failed to fetch URL or received empty response: " + url));
    }

    std::string text = html_to_text(response.body);
    if (text.empty()) {
        return u8str_util::to_u8str(result_json(false, "", "No extractable content found at: " + url));
    }

    // 兜底清洗：防止非法 UTF-8 字节流入 JSON 序列化导致异常
    text = u8str_util::to_string(llm::sanitize_utf8(u8str_util::to_u8str(text)));

    json result;
    result["url"] = url;
    result["content_length"] = response.body.size();
    result["content"] = text;
    return u8str_util::to_u8str(result_json(true, safe_dump(result)));
}

void WebFetchTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool WebFetchTool::requires_confirmation() const { return false; }

// ============================================================
// Factory
// ============================================================

ToolPtr create_web_fetch_tool() {
    return std::make_shared<WebFetchTool>();
}

} // namespace agent
