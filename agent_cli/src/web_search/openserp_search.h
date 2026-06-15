#pragma once
#include "web_search_tool_base.h"
#include <string>
#include <vector>

namespace agent {

class OpenSerpSearch : public WebSearchToolBase {
public:
    explicit OpenSerpSearch(std::vector<std::string> engines);
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;

protected:
    std::vector<WebSearchResult> do_search(const std::string& query, int count) override;
    u8str get_tool_name() const override;
    u8str get_tool_description() const override;

private:
    std::vector<std::string> engines_;
};

} // namespace agent