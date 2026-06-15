#include "mcp_manager.h"

namespace agent {

void McpManager::register_mcp(McpPtr mcp) {
    std::unique_lock lock(mutex_);
    mcps_[mcp->name()] = std::move(mcp);
}

McpPtr McpManager::get_mcp(const u8str& name) const {
    std::shared_lock lock(mutex_);
    auto it = mcps_.find(name);
    if (it != mcps_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<McpPtr> McpManager::list_mcps() const {
    std::shared_lock lock(mutex_);
    std::vector<McpPtr> result;
    result.reserve(mcps_.size());
    for (const auto& [_, mcp] : mcps_) {
        result.push_back(mcp);
    }
    return result;
}

bool McpManager::has_mcp(const u8str& name) const {
    std::shared_lock lock(mutex_);
    return mcps_.find(name) != mcps_.end();
}

} // namespace agent
