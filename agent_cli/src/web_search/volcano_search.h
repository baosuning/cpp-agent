#pragma once
#include "web_search_tool_base.h"
#include <string>

namespace agent {

class VolcanoSearch : public WebSearchToolBase {
public:
    explicit VolcanoSearch(std::string api_key);
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;

protected:
    std::vector<WebSearchResult> do_search(const std::string& query, int count) override;
    u8str get_tool_name() const override;
    u8str get_tool_description() const override;

private:
    std::string api_key_;
};

} // namespace agent