#pragma once
#include <string>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace agent {
namespace mcp {

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    virtual nlohmann::json send_request(const nlohmann::json& request, int timeout_ms = 30000) = 0;

    virtual void send_notification(const nlohmann::json& notification, int timeout_ms = 30000) = 0;

    virtual std::string name() const = 0;
};

using TransportPtr = std::unique_ptr<ITransport>;

TransportPtr create_stdio_transport(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::map<std::string, std::string>& env,
    int timeout_ms = 30000);

TransportPtr create_http_transport(
    const std::string& url,
    int timeout_ms = 30000);

} // namespace mcp
} // namespace agent