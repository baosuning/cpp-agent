#pragma once
#include "types.h"
#include <vector>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

// 使用者无需实现IMcpClient和IMcpManager, 由Agent内部创建并传递给Loop使用

namespace agent {

// MCP 客户端抽象接口，用于解耦对 McpClient 对象的直接依赖
class IMcpClient {
public:
    virtual ~IMcpClient() = default;

    virtual u8str name() const = 0;
    virtual u8str call(const u8str& method, const u8str& params) = 0;
    virtual std::vector<nlohmann::json> list_tools() const = 0;
};

using McpPtr = std::shared_ptr<IMcpClient>;

// MCP 管理器抽象接口，用于解耦 Agent 对 MCP 管理器的直接依赖
class IMcpManager {
public:
    virtual ~IMcpManager() = default;
    virtual void register_mcp(McpPtr mcp) = 0;
    virtual McpPtr get_mcp(const u8str& name) const = 0;
    virtual std::vector<McpPtr> list_mcps() const = 0;
    virtual bool has_mcp(const u8str& name) const = 0;
};

using McpManagerPtr = std::shared_ptr<IMcpManager>;

} // namespace agent
