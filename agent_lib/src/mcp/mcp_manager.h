#pragma once
#include <agent/i_mcp.h>
#include <shared_mutex>
#include <map>

namespace agent {

class McpManager : public IMcpManager {
public:
    void register_mcp(McpPtr mcp) override;
    McpPtr get_mcp(const u8str& name) const override;
    std::vector<McpPtr> list_mcps() const override;
    bool has_mcp(const u8str& name) const override;

private:
    mutable std::shared_mutex    mutex_;
    std::map<u8str, McpPtr>      mcps_;
};

} // namespace agent
