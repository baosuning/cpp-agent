#pragma once
// agent_cli/src/web_search/bocha_search.h
#include "web_search_tool_base.h"
#include <string>

namespace agent {

std::vector<WebSearchResult> search_bocha(const std::string& query, int count, const std::string& api_key);

class BochaSearch : public WebSearchToolBase {
public:
    explicit BochaSearch(std::string api_key);
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;

protected:
    std::vector<WebSearchResult> do_search(const std::string& query, int count) override;
    u8str get_tool_name() const override;
    u8str get_tool_description() const override;
    int    get_default_count() const override { return 10; }
    int    get_max_count() const override { return 50; }
    std::string get_failure_message() const override {
        return "Bocha search failed. Check BOCHA_API_KEY environment variable and network connectivity.";
    }

private:
    std::string api_key_;
};

} // namespace agent
