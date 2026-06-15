// agent_cli/src/web_search/bing_search.cpp
#include "bing_search.h"
#include "web_search_common.h"
#include <util/log.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace agent {

std::vector<WebSearchResult> search_bing(const std::string& query, int count, const std::string& api_key) {
    if (api_key.empty()) {
        AGENT_LOG_WARN("Bing") << "API key is empty";
        return {};
    }

    std::string encoded_q = url_encode(query);
    std::string path = "/v7.0/search?q=" + encoded_q + "&count=" + std::to_string(count) + "&mkt=zh-CN";
    std::string response = winhttp_get_with_header("api.bing.microsoft.com", path, true,
                                                     "Ocp-Apim-Subscription-Key", api_key);
    if (response.empty()) {
        AGENT_LOG_ERROR("Bing") << "Empty response for query: " << query;
        return {};
    }

    std::vector<WebSearchResult> results;
    try {
        auto json_resp = nlohmann::json::parse(response);
        if (json_resp.contains("webPages") && json_resp["webPages"].contains("value")) {
            for (const auto& item : json_resp["webPages"]["value"]) {
                WebSearchResult entry;
                entry.title = item.contains("name") ? item["name"].get<std::string>() : "";
                entry.url = item.contains("url") ? item["url"].get<std::string>() : "";
                entry.snippet = item.contains("snippet") ? item["snippet"].get<std::string>() : "";
                results.push_back(std::move(entry));
            }
        } else if (json_resp.contains("error")) {
            AGENT_LOG_ERROR("Bing") << "API error: " << json_resp["error"].dump();
        }
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("Bing") << "Parse error: " << e.what();
    }

    return results;
}

BingSearch::BingSearch(std::string api_key)
    : api_key_(std::move(api_key)) {}

u8str BingSearch::name() const {
    return get_tool_name();
}

u8str BingSearch::description() const {
    return get_tool_description();
}

u8str BingSearch::parameters_schema() const {
    return to_u8str(R"schema({
        "type": "object",
        "properties": {
            "query": {"type": "string", "description": "The search query"},
            "count": {"type": "integer", "description": "Number of results to return, max 20", "default": 5}
        },
        "required": ["query"]
    })schema");
}

std::vector<WebSearchResult> BingSearch::do_search(const std::string& query, int count) {
    return search_bing(query, count, api_key_);
}

u8str BingSearch::get_tool_name() const {
    return u8str(u8"bing_search");
}

u8str BingSearch::get_tool_description() const {
    return u8str(u8"Search the web using Bing API. Returns title, url and snippet for each result.");
}

} // namespace agent
