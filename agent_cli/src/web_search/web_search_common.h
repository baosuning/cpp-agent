#pragma once
#include <agent/i_tool.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace agent {

using json = nlohmann::json;

struct WebSearchResult {
    std::string title;
    std::string url;
    std::string snippet;
};

std::string to_std(const u8str& s);
u8str to_u8str(const std::string& s);
json parse_args(const u8str& arguments);
std::string result_json(bool success, const std::string& data, const std::string& error = "");
std::string url_encode(const std::string& s);
std::string format_search_results(const std::string& query, const std::vector<WebSearchResult>& results);

std::string winhttp_get(const std::string& host, const std::string& path, bool is_https, int timeout_seconds = 30, int custom_port = 0);
std::string winhttp_get_with_header(const std::string& host, const std::string& path, bool is_https,
                                     const std::string& header_name, const std::string& header_value,
                                     int timeout_seconds = 30);
std::string winhttp_get_with_headers(const std::string& host, const std::string& path, bool is_https,
                                     const std::vector<std::pair<std::string, std::string>>& headers,
                                     int timeout_seconds = 30, int custom_port = 0);
std::string winhttp_post(const std::string& host, const std::string& path, bool is_https,
                          const std::string& content_type, const std::string& post_data,
                          const std::string& auth_header, int timeout_seconds = 30);



} // namespace agent