#pragma once
// agent_cli/src/web_search/baidu_ai_search.h
#include "web_search_tool_base.h"
#include <string>

namespace agent {

std::vector<WebSearchResult> search_baidu_ai(const std::string& query, int count, const std::string& api_key);

class BaiduAiSearch : public WebSearchToolBase {
public:
    explicit BaiduAiSearch(std::string api_key);
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;

protected:
    std::vector<WebSearchResult> do_search(const std::string& query, int count) override;
    u8str get_tool_name() const override;
    u8str get_tool_description() const override;
    int    get_default_count() const override { return 10; }
    int    get_max_count() const override { return 20; }
    std::string get_failure_message() const override {
        return "Baidu AI Search failed. Check BAIDU_AI_SEARCH_KEY environment variable and network connectivity.";
    }

private:
    std::string api_key_;
};

} // namespace agent
