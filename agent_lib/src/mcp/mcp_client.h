#pragma once
#include "mcp_transport.h"
#include "json_rpc.h"
#include <agent/i_mcp.h>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>
#include <agent/types.h>

namespace agent {
namespace mcp {

struct McpClientConfig {
    std::string                        name;
    std::string                        description;

    std::string                        command;
    std::vector<std::string>           args;
    std::map<std::string, std::string> env;

    std::string                        url;

    int                                timeout_ms = 30000;
    bool                               debug = false;
    // 配置中存在未设置的环境变量占位符时置为 true，调用方应静默跳过该 MCP
    bool                               skip = false;

    static McpClientConfig             from_json(const nlohmann::json& j, const std::string& client_name);
};

class McpClient : public IMcpClient {
public:
    explicit McpClient(McpClientConfig config);
    ~McpClient() override;

    // IMcpClient 接口实现
    u8str name() const override;
    u8str call(const u8str& method, const u8str& params) override;
    std::vector<nlohmann::json> list_tools() const override;

    // 内部方法（非接口）
    u8str config() const;
    bool connect();
    void disconnect();
    void call_async(const u8str& method, const u8str& params,
                    std::function<void(u8str)> callback);
    bool is_connected() const;

private:
    std::string       to_std(const u8str& s) const;
    u8str             to_u8(const std::string& s) const;
    nlohmann::json    send_jsonrpc(const std::string& method, const nlohmann::json& params);

private:
    McpClientConfig                config_;
    TransportPtr                   transport_;
    std::atomic<bool>              connected_{false};
    std::mutex                     mutex_;
    std::vector<nlohmann::json>    tools_;
    int64_t                        request_id_ = 1;
};

} // namespace mcp
} // namespace agent
