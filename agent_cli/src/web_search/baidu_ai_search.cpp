// agent_cli/src/web_search/baidu_ai_search.cpp
#include "baidu_ai_search.h"
#include "web_search_common.h"
#include <util/log.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace agent {

std::vector<WebSearchResult> search_baidu_ai(const std::string& query, int count, const std::string& api_key) {
    if (api_key.empty()) return {};

    json body;
    json msg;
    msg["role"] = "user";
    msg["content"] = query;
    body["messages"] = json::array({msg});
    body["model"] = "ernie-4.5-turbo-128k";
    body["stream"] = false;
    body["search_source"] = "baidu_search_v2";
    body["enable_corner_markers"] = false;

    json resource_filter = json::array();
    json web_filter;
    web_filter["type"] = "web";
    web_filter["top_k"] = count;
    resource_filter.push_back(web_filter);
    body["resource_type_filter"] = resource_filter;

    std::string post_body = body.dump();
    std::string auth = "Bearer " + api_key;

    AGENT_LOG_DEBUG("BaiduAi") << "Searching for: " << query;
    std::string response = winhttp_post("qianfan.baidubce.com", "/v2/ai_search/chat/completions",
                                         true, "application/json", post_body, auth, 60);

    if (response.empty()) {
        AGENT_LOG_ERROR("BaiduAi") << "Empty response for query: " << query;
        return {};
    }

    std::vector<WebSearchResult> results;
    try {
        auto json_resp = nlohmann::json::parse(response);

        if (json_resp.contains("code")) {
            std::string err_msg = json_resp.value("message", "unknown error");
            AGENT_LOG_ERROR("BaiduAi") << "API error " << json_resp["code"] << ": " << err_msg;
            return {};
        }

        std::string ai_summary;
        if (json_resp.contains("choices") && json_resp["choices"].is_array()
            && !json_resp["choices"].empty()
            && json_resp["choices"][0].contains("message")
            && json_resp["choices"][0]["message"].contains("content")) {
            ai_summary = json_resp["choices"][0]["message"]["content"].get<std::string>();
        }

        if (!ai_summary.empty()) {
            WebSearchResult summary_entry;
            summary_entry.title = "百度AI搜索智能摘要";
            summary_entry.url = "https://cloud.baidu.com/product/ai-search";
            summary_entry.snippet = ai_summary;
            results.push_back(std::move(summary_entry));
        }

        if (json_resp.contains("references") && json_resp["references"].is_array()) {
            int web_count = 0;
            for (const auto& ref : json_resp["references"]) {
                std::string type = ref.value("type", "web");
                if (type != "web") continue;

                WebSearchResult entry;
                entry.title = ref.contains("title") && !ref["title"].is_null()
                    ? ref["title"].get<std::string>() : "";
                entry.url = ref.contains("url") && !ref["url"].is_null()
                    ? ref["url"].get<std::string>() : "";
                entry.snippet = ref.contains("content") && !ref["content"].is_null()
                    ? ref["content"].get<std::string>() : "";

                if (!entry.title.empty() && !entry.url.empty()) {
                    results.push_back(std::move(entry));
                    ++web_count;
                }

                if (web_count >= count) break;
            }
        }
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("BaiduAi") << "Parse error: " << e.what();
    }

    return results;
}

BaiduAiSearch::BaiduAiSearch(std::string api_key)
    : api_key_(std::move(api_key)) {}

u8str BaiduAiSearch::name() const {
    return get_tool_name();
}

u8str BaiduAiSearch::description() const {
    return get_tool_description();
}

u8str BaiduAiSearch::parameters_schema() const {
    return to_u8str(R"schema({
        "type": "object",
        "properties": {
            "query": {"type": "string", "description": "The search query"},
            "count": {"type": "integer", "description": "Number of search results to return, max 20", "default": 10}
        },
        "required": ["query"]
    })schema");
}

std::vector<WebSearchResult> BaiduAiSearch::do_search(const std::string& query, int count) {
    return search_baidu_ai(query, count, api_key_);
}

u8str BaiduAiSearch::get_tool_name() const {
    return u8str(u8"baidu_ai_search");
}

u8str BaiduAiSearch::get_tool_description() const {
    return u8str(u8"Search the web using Baidu AI Search. Powered by 百度千帆AI搜索, this tool searches the internet and uses AI to generate a comprehensive summary with referenced sources. Returns an AI-generated summary followed by individual search results with title, url and snippet.");
}

} // namespace agent
