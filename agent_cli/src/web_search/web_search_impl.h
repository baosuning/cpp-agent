#pragma once
#include "web_search_common.h"
#include "bing_search.h"
#include "bocha_search.h"
#include "volcano_search.h"
#include "openserp_search.h"
#include "baidu_ai_search.h"
#include <vector>
#include <memory>

namespace agent {

std::shared_ptr<ITool> create_bing_search_tool(const std::string& api_key);
std::shared_ptr<ITool> create_bocha_search_tool(const std::string& api_key);
std::shared_ptr<ITool> create_volcano_search_tool(const std::string& api_key);
std::shared_ptr<ITool> create_openserp_search_tool(const std::vector<std::string>& engines = {"Bing"});
std::shared_ptr<ITool> create_baidu_ai_search_tool(const std::string& api_key);

} // namespace agent
