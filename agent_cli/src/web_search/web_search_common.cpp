#include "web_search_common.h"
#include <util/i_http_client.h>
#include <util/log.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <regex>

namespace agent {

std::string to_std(const u8str& s) {
    return std::string(s.begin(), s.end());
}

u8str to_u8str(const std::string& s) {
    return u8str(s.begin(), s.end());
}

json parse_args(const u8str& arguments) {
    try {
        return json::parse(to_std(arguments));
    } catch (...) {
        return json::object();
    }
}

std::string result_json(bool success, const std::string& data, const std::string& error) {
    json j;
    j["success"] = success;
    if (success) {
        j["result"] = data;
    } else {
        j["error"] = error;
    }
    return j.dump();
}

std::string url_encode(const std::string& s) {
    std::string result;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += c;
        } else if (c == ' ') {
            result += '+';
        } else {
            char buf[8];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            result += buf;
        }
    }
    return result;
}

std::string format_search_results(const std::string& query, const std::vector<WebSearchResult>& results) {
    json arr = json::array();
    for (const auto& r : results) {
        json entry;
        entry["title"] = r.title;
        entry["url"] = r.url;
        entry["snippet"] = r.snippet;
        arr.push_back(std::move(entry));
    }
    json result;
    result["query"] = query;
    result["results"] = std::move(arr);
    return result.dump();
}

#ifdef _WIN32

static std::string build_url(const std::string& host, const std::string& path, bool is_https, int port = 0) {
    std::string scheme = is_https ? "https://" : "http://";
    std::string url = scheme + host;
    if (port > 0) {
        url += ":" + std::to_string(port);
    }
    url += path;
    return url;
}

std::string winhttp_get(const std::string& host, const std::string& path, bool is_https, int timeout_seconds, int custom_port) {
    std::string url = build_url(host, path, is_https, custom_port);
    auto client = create_http_client();
    if (!client) {
        AGENT_LOG_ERROR("HTTP") << "Failed to create HTTP client for: " << url;
        return "";
    }

    std::map<std::string, std::string> headers = {
        {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"}
    };

    auto response = client->get(url, headers, timeout_seconds * 1000);
    if (response.is_error) {
        AGENT_LOG_ERROR("HTTP") << "GET error: " << response.error_message;
    } else if (response.status_code != 200) {
        AGENT_LOG_ERROR("HTTP") << "GET failed with status " << response.status_code << ": " << url;
    }
    return response.body;
}

std::string winhttp_get_with_header(const std::string& host, const std::string& path, bool is_https,
                                     const std::string& header_name, const std::string& header_value,
                                     int timeout_seconds) {
    std::string url = build_url(host, path, is_https);
    auto client = create_http_client();
    if (!client) {
        AGENT_LOG_ERROR("HTTP") << "Failed to create HTTP client for: " << url;
        return "";
    }

    std::map<std::string, std::string> headers = {
        {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"}
    };
    if (!header_name.empty() && !header_value.empty()) {
        headers[header_name] = header_value;
    }

    auto response = client->get(url, headers, timeout_seconds * 1000);
    if (response.is_error) {
        AGENT_LOG_ERROR("HTTP") << "GET error: " << response.error_message;
    } else if (response.status_code != 200) {
        AGENT_LOG_ERROR("HTTP") << "GET failed with status " << response.status_code << ": " << url;
    }
    return response.body;
}

std::string winhttp_get_with_headers(const std::string& host, const std::string& path, bool is_https,
                                     const std::vector<std::pair<std::string, std::string>>& headers,
                                     int timeout_seconds, int custom_port) {
    std::string url = build_url(host, path, is_https, custom_port);
    auto client = create_http_client();
    if (!client) {
        AGENT_LOG_ERROR("HTTP") << "Failed to create HTTP client for: " << url;
        return "";
    }

    std::map<std::string, std::string> header_map;
    header_map["User-Agent"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";
    for (const auto& [key, value] : headers) {
        if (!key.empty() && !value.empty()) {
            header_map[key] = value;
        }
    }

    auto response = client->get(url, header_map, timeout_seconds * 1000);
    if (response.is_error) {
        AGENT_LOG_ERROR("HTTP") << "GET error: " << response.error_message;
    } else if (response.status_code != 200) {
        AGENT_LOG_ERROR("HTTP") << "GET failed with status " << response.status_code << ": " << url;
    }
    return response.body;
}

std::string winhttp_post(const std::string& host, const std::string& path, bool is_https,
                          const std::string& content_type, const std::string& post_data,
                          const std::string& auth_header, int timeout_seconds) {
    std::string url = build_url(host, path, is_https);
    auto client = create_http_client();
    if (!client) {
        AGENT_LOG_ERROR("HTTP") << "Failed to create HTTP client for: " << url;
        return "";
    }

    std::map<std::string, std::string> headers = {
        {"User-Agent", "Agent-Framework/1.0"},
        {"Content-Type", content_type}
    };
    if (!auth_header.empty()) {
        headers["Authorization"] = auth_header;
    }

    auto response = client->post(url, post_data, headers, timeout_seconds * 1000);
    if (response.is_error) {
        AGENT_LOG_ERROR("HTTP") << "POST error: " << response.error_message;
    } else if (response.status_code != 200 && response.status_code != 201) {
        AGENT_LOG_ERROR("HTTP") << "POST failed with status " << response.status_code << ": " << url;
    }
    return response.body;
}

#endif

} // namespace agent