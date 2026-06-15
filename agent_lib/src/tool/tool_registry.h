#pragma once
#include <agent/i_tool.h>
#include <shared_mutex>
#include <map>

namespace agent {

class ToolRegistry : public IToolRegistry {
public:
    void register_tool(ToolPtr tool) override;
    void update_tool(const u8str& name, ToolPtr tool) override;
    void remove_tool(const u8str& name) override;
    ToolPtr get_tool(const u8str& name) const override;
    std::vector<ToolPtr> list_tools() const override;
    bool has_tool(const u8str& name) const override;
    nlohmann::json get_tools_schema() const override;

private:
    mutable std::shared_mutex   mutex_;
    std::map<u8str, ToolPtr>    tools_;
};

} // namespace agent
