#include "tool_registry.h"
#include <agent/i_tool.h>

namespace agent {

ToolRegistryPtr create_tool_registry() {
    return std::make_shared<ToolRegistry>();
}

void ToolRegistry::register_tool(ToolPtr tool) {
    std::unique_lock lock(mutex_);
    tools_[tool->name()] = std::move(tool);
}

void ToolRegistry::update_tool(const u8str& name, ToolPtr tool) {
    std::unique_lock lock(mutex_);
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        tools_.erase(it);
        tools_[tool->name()] = std::move(tool);
    }
}

void ToolRegistry::remove_tool(const u8str& name) {
    std::unique_lock lock(mutex_);
    tools_.erase(name);
}

ToolPtr ToolRegistry::get_tool(const u8str& name) const {
    std::shared_lock lock(mutex_);
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<ToolPtr> ToolRegistry::list_tools() const {
    std::shared_lock lock(mutex_);
    std::vector<ToolPtr> result;
    result.reserve(tools_.size());
    for (const auto& [_, tool] : tools_) {
        result.push_back(tool);
    }
    return result;
}

bool ToolRegistry::has_tool(const u8str& name) const {
    std::shared_lock lock(mutex_);
    return tools_.find(name) != tools_.end();
}

nlohmann::json ToolRegistry::get_tools_schema() const {
    std::shared_lock lock(mutex_);
    nlohmann::json schema = nlohmann::json::array();
    for (const auto& [_, tool] : tools_) {
        nlohmann::json entry;
        entry["type"] = "function";
        {
            auto name = tool->name();
            std::string name_str(name.begin(), name.end());
            entry["function"]["name"] = name_str;
        }
        {
            auto desc = tool->description();
            std::string desc_str(desc.begin(), desc.end());
            entry["function"]["description"] = desc_str;
        }
        {
            auto params = tool->parameters_schema();
            std::string params_str(params.begin(), params.end());
            entry["function"]["parameters"] = nlohmann::json::parse(params_str);
        }
        schema.push_back(std::move(entry));
    }
    return schema;
}

} // namespace agent
