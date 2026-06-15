#include "mcp_client.h"
#include "../util/utf8_utils.h"
#include <util/log.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <regex>
#include <cstdlib>

// MCP 调试日志（底层使用 util/log.h 的统一日志系统）
#define MCP_DEBUG(...) AGENT_LOG_DEBUG_IF(config_.debug, "McpClient") << __VA_ARGS__

namespace agent {
namespace mcp {
namespace {

// 展开字符串中的 ${VAR} 环境变量占位符
std::string expand_env_vars(const std::string& input) {
    static const std::regex env_regex(R"(\$\{([^}]+)\})");
    std::string result;
    result.reserve(input.size());

    std::sregex_iterator it(input.begin(), input.end(), env_regex);
    std::sregex_iterator end;
    size_t last_pos = 0;

    for (; it != end; ++it) {
        result.append(input, last_pos, it->position() - last_pos);
        const char* env_val = std::getenv((*it)[1].str().c_str());
        if (env_val) {
            result.append(env_val);
        }
        last_pos = it->position() + it->length();
    }
    result.append(input, last_pos, input.size() - last_pos);
    return result;
}

}  // namespace

McpClientConfig McpClientConfig::from_json(const nlohmann::json& j, const std::string& client_name) {
    McpClientConfig cfg;
    cfg.name = client_name;

    if (j.contains("description")) {
        cfg.description = j["description"].get<std::string>();
    }

    if (j.contains("url")) {
        cfg.url = expand_env_vars(j["url"].get<std::string>());
    } else if (j.contains("command")) {
        cfg.command = expand_env_vars(j["command"].get<std::string>());
        if (j.contains("args") && j["args"].is_array()) {
            for (const auto& arg : j["args"]) {
                cfg.args.push_back(expand_env_vars(arg.get<std::string>()));
            }
        }
        if (j.contains("env") && j["env"].is_object()) {
            for (auto it = j["env"].begin(); it != j["env"].end(); ++it) {
                cfg.env[it.key()] = expand_env_vars(it.value().get<std::string>());
            }
        }
    } else {
        throw std::runtime_error("MCP client '" + client_name +
            "': must have either 'url' (HTTP) or 'command' (stdio) field");
    }

    if (j.contains("timeout")) {
        cfg.timeout_ms = j["timeout"].get<int>();
    }

    return cfg;
}

McpClient::McpClient(McpClientConfig config)
    : config_(std::move(config)) {}

McpClient::~McpClient() {
    disconnect();
}

u8str McpClient::name() const {
    return to_u8(config_.name);
}

u8str McpClient::config() const {
    nlohmann::json j;
    j["name"] = config_.name;
    j["description"] = config_.description;
    j["timeout_ms"] = config_.timeout_ms;
    if (!config_.url.empty()) {
        j["transport"] = "http";
        j["url"] = config_.url;
    } else {
        j["transport"] = "stdio";
        j["command"] = config_.command;
        j["args"] = config_.args;
    }
    std::vector<nlohmann::json> tool_list;
    for (const auto& tool : tools_) {
        tool_list.push_back(tool);
    }
    if (!tool_list.empty()) {
        j["tools"] = tool_list;
    }
    return to_u8(j.dump());
}

bool McpClient::connect() {
    if (connected_) return true;

    if (!config_.url.empty()) {
        transport_ = create_http_transport(config_.url, config_.timeout_ms);
    } else if (!config_.command.empty()) {
        transport_ = create_stdio_transport(
            config_.command, config_.args, config_.env, config_.timeout_ms);
    } else {
        AGENT_LOG_WARN("McpClient") << "No transport configuration for '" << config_.name << "'";
        return false;
    }

    if (!transport_->connect()) {
        AGENT_LOG_ERROR("McpClient") << "Failed to connect to MCP server '" << config_.name << "'";
        return false;
    }

    try {
        nlohmann::json init_params;
        init_params["protocolVersion"] = "2024-11-05";
        init_params["capabilities"] = nlohmann::json::object();
        init_params["clientInfo"]["name"] = "agent-framework";
        init_params["clientInfo"]["version"] = "0.1.0";

        auto init_result = send_jsonrpc("initialize", init_params);

        auto notification = jsonrpc::Request::make_notification(
                "notifications/initialized", nlohmann::json::object());
        transport_->send_notification(notification.to_json(), config_.timeout_ms);

        MCP_DEBUG("Connected to server '" << config_.name << "'");

        try {
            auto tools_result = send_jsonrpc("tools/list", nlohmann::json::object());
            if (tools_result.contains("tools") && tools_result["tools"].is_array()) {
                tools_ = tools_result["tools"].get<std::vector<nlohmann::json>>();
                MCP_DEBUG("Server '" << config_.name << "' provides "
                          << tools_.size() << " tool(s)");
            }
        } catch (const std::exception& e) {
            AGENT_LOG_WARN("McpClient") << "tools/list failed for '"
                      << config_.name << "': " << e.what();
        }

        connected_ = true;
        return true;

    } catch (const std::exception& e) {
        AGENT_LOG_WARN("McpClient") << "initialize failed for '"
                  << config_.name << "': " << e.what();
        transport_->disconnect();
        connected_ = false;
        return false;
    }
}

void McpClient::disconnect() {
    if (!connected_) return;

    try {
        send_jsonrpc("shutdown", nlohmann::json::object());
    } catch (...) {
        AGENT_LOG_DEBUG("McpClient") << "shutdown failed for '" << config_.name << "' (non-critical)";
    }

    if (transport_) {
        transport_->disconnect();
    }
    transport_.reset();
    connected_ = false;
    tools_.clear();
}

u8str McpClient::call(const u8str& method, const u8str& params) {
    std::string method_str = to_std(method);
    u8str clean_params = llm::sanitize_utf8(params);
    std::string params_str = to_std(clean_params);

    if (!connected_) {
        return to_u8("Error: MCP client '" + config_.name + "' is not connected to server");
    }

    try {
        nlohmann::json json_params;
        try {
            if (!params_str.empty()) {
                json_params = nlohmann::json::parse(params_str);
            }
        } catch (...) {
            MCP_DEBUG("Params parse failed, using as raw input");
            json_params["input"] = params_str;
        }

        nlohmann::json rpc_params;
        rpc_params["name"] = method_str;
        rpc_params["arguments"] = json_params;

        auto result = send_jsonrpc("tools/call", rpc_params);

        if (result.contains("content") && result["content"].is_array()) {
            std::string output;
            for (const auto& part : result["content"]) {
                if (part.contains("type") && part["type"] == "text" && part.contains("text")) {
                    output += part["text"].get<std::string>();
                }
            }
            if (!output.empty()) {
                return to_u8(output);
            }
        }

        if (result.contains("isError") && result["isError"].get<bool>()) {
            return to_u8("Error: MCP tool execution failed for '" + method_str + "'");
        }

        return to_u8(result.dump());
    } catch (const std::exception& e) {
        return to_u8("Error: MCP call failed for '" + config_.name +
                     "." + method_str + "': " + std::string(e.what()));
    }
}

void McpClient::call_async(const u8str& method, const u8str& params,
                           std::function<void(u8str)> callback) {
    callback(call(method, params));
}

bool McpClient::is_connected() const {
    return connected_;
}

std::vector<nlohmann::json> McpClient::list_tools() const {
    return tools_;
}

nlohmann::json McpClient::send_jsonrpc(const std::string& method,
                                        const nlohmann::json& params) {
    if (!transport_ || !transport_->is_connected()) {
        throw std::runtime_error("McpClient: transport not connected to server");
    }

    auto request = jsonrpc::Request::make_request(method, params, request_id_++);
    auto response_json = transport_->send_request(request.to_json(), config_.timeout_ms);

    auto response = jsonrpc::Response::from_json(response_json);

    if (response.is_error()) {
        auto& err = *response.error;
        std::string err_msg = "Server returned JSON-RPC error [" + std::to_string(err.code) + "]: " + err.message;
        throw std::runtime_error(err_msg);
    }

    if (!response.result) {
        throw std::runtime_error("Server response missing 'result' field");
    }

    return *response.result;
}

std::string McpClient::to_std(const u8str& s) const {
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

u8str McpClient::to_u8(const std::string& s) const {
    return u8str(reinterpret_cast<const char8_t*>(s.data()), s.size());
}

} // namespace mcp
} // namespace agent