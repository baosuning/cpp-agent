// agent_cli/src/web_search/bocha_search.cpp
#include "bocha_search.h"
#include "web_search_common.h"
#include <util/log.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace agent {

std::vector<WebSearchResult> search_bocha(const std::string& query, int count, const std::string& api_key) {
    if (api_key.empty()) return {};

    json body;
    body["query"] = query;
    body["summary"] = true;
    body["freshness"] = "noLimit";
    body["count"] = count;

    std::string post_body = body.dump();
    std::string auth = "Bearer " + api_key;
    std::string response = winhttp_post("api.bochaai.com", "/v1/web-search", true,
                                         "application/json", post_body, auth);

    if (response.empty()) {
        AGENT_LOG_ERROR("Bocha") << "Empty response for query: " << query;
        return {};
    }

    std::vector<WebSearchResult> results;
    try {
        auto json_resp = nlohmann::json::parse(response);

        if (json_resp.contains("data") && json_resp["data"].contains("webPages")
            && json_resp["data"]["webPages"].contains("value")) {
            for (const auto& item : json_resp["data"]["webPages"]["value"]) {
                WebSearchResult entry;
                entry.title = item.contains("name") ? item["name"].get<std::string>() : "";
                entry.url = item.contains("url") ? item["url"].get<std::string>() : "";
                if (item.contains("summary") && !item["summary"].is_null()) {
                    entry.snippet = item["summary"].get<std::string>();
                } else if (item.contains("snippet") && !item["snippet"].is_null()) {
                    entry.snippet = item["snippet"].get<std::string>();
                }
                results.push_back(std::move(entry));
            }
        } else {
            AGENT_LOG_WARN("Bocha") << "Unexpected response structure";
        }
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("Bocha") << "Parse error: " << e.what();
    }

    return results;
}

BochaSearch::BochaSearch(std::string api_key)
    : api_key_(std::move(api_key)) {}

u8str BochaSearch::name() const {
    return get_tool_name();
}

u8str BochaSearch::description() const {
    return get_tool_description();
}

u8str BochaSearch::parameters_schema() const {
    return to_u8str(R"schema({
        "type": "object",
        "properties": {
            "query": {"type": "string", "description": "The search query"},
            "count": {"type": "integer", "description": "Number of results to return, max 50", "default": 10}
        },
        "required": ["query"]
    })schema");
}

std::vector<WebSearchResult> BochaSearch::do_search(const std::string& query, int count) {
    return search_bocha(query, count, api_key_);
}

u8str BochaSearch::get_tool_name() const {
    return u8str(u8"bocha_search");
}

u8str BochaSearch::get_tool_description() const {
    return u8str(u8"Search the web for information. Powered by Bocha AI. Returns title, url and snippet for each result. Supports natural language queries and provides accurate, up-to-date results.");
}

} // namespace agent
