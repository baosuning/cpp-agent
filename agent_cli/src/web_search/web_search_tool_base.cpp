// agent_cli/src/web_search/web_search_tool_base.cpp
#include "web_search_tool_base.h"
#include <nlohmann/json.hpp>
#include <string>

namespace agent {

u8str WebSearchToolBase::execute(const u8str& arguments) {
    json args = parse_args(arguments);

    auto query_it = args.find("query");
    if (query_it == args.end() || !query_it->is_string()) {
        return to_u8str(result_json(false, "", "Missing required parameter: query"));
    }

    int count = get_default_count();
    int max_count = get_max_count();
    auto count_it = args.find("count");
    if (count_it != args.end() && count_it->is_number_integer()) {
        count = count_it->get<int>();
        if (count < 1) count = 1;
        if (count > max_count) count = max_count;
    }

    std::string query = query_it->get<std::string>();
    std::vector<WebSearchResult> results = do_search(query, count);

    if (results.empty()) {
        return to_u8str(result_json(false, "", get_failure_message()));
    }
    return to_u8str(result_json(true, format_search_results(query, results)));
}

void WebSearchToolBase::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

}  // namespace agent
