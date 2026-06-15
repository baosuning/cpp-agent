#include "web_search_impl.h"

namespace agent {

ToolPtr create_bing_search_tool(const std::string& api_key) {
    return std::make_shared<BingSearch>(api_key);
}

ToolPtr create_bocha_search_tool(const std::string& api_key) {
    return std::make_shared<BochaSearch>(api_key);
}

ToolPtr create_volcano_search_tool(const std::string& api_key) {
    return std::make_shared<VolcanoSearch>(api_key);
}

ToolPtr create_openserp_search_tool(const std::vector<std::string>& engines) {
    return std::make_shared<OpenSerpSearch>(engines);
}

ToolPtr create_baidu_ai_search_tool(const std::string& api_key) {
    return std::make_shared<BaiduAiSearch>(api_key);
}

} // namespace agent