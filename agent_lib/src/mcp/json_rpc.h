#pragma once
#include <agent/types.h>
#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <cstdint>

namespace agent {
namespace jsonrpc {

constexpr const char* JSON_RPC_VERSION = "2.0";

enum class ErrorCode : int32_t {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    ServerErrorStart = -32000,
    ServerErrorEnd = -32099,
};

struct Error {
    int32_t code = 0;
    std::string message;
    std::optional<nlohmann::json> data;

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["code"] = code;
        j["message"] = message;
        if (data) {
            j["data"] = *data;
        }
        return j;
    }

    static Error from_json(const nlohmann::json& j) {
        Error err;
        if (j.contains("code")) err.code = j["code"].get<int32_t>();
        if (j.contains("message")) err.message = j["message"].get<std::string>();
        if (j.contains("data") && !j["data"].is_null()) {
            err.data = j["data"];
        }
        return err;
    }

    static Error make(ErrorCode code, const std::string& msg, std::optional<nlohmann::json> data = std::nullopt) {
        Error err;
        err.code = static_cast<int32_t>(code);
        err.message = msg;
        err.data = std::move(data);
        return err;
    }
};

struct Request {
    std::string method;
    nlohmann::json params = nlohmann::json::object();
    std::optional<nlohmann::json> id = std::nullopt;

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["jsonrpc"] = JSON_RPC_VERSION;
        j["method"] = method;
        if (!params.is_null()) {
            j["params"] = params;
        }
        if (id) {
            j["id"] = *id;
        }
        return j;
    }

    static Request from_json(const nlohmann::json& j) {
        Request req;
        if (j.contains("method")) req.method = j["method"].get<std::string>();
        if (j.contains("params") && !j["params"].is_null()) {
            req.params = j["params"];
        }
        if (j.contains("id") && !j["id"].is_null()) {
            req.id = j["id"];
        }
        return req;
    }

    static Request make_request(const std::string& method, const nlohmann::json& params, int64_t id = 1) {
        Request req;
        req.method = method;
        req.params = params;
        req.id = id;
        return req;
    }

    static Request make_notification(const std::string& method, const nlohmann::json& params) {
        Request req;
        req.method = method;
        req.params = params;
        req.id = std::nullopt;
        return req;
    }
};

struct Response {
    std::optional<nlohmann::json> id = std::nullopt;
    std::optional<nlohmann::json> result = std::nullopt;
    std::optional<Error> error = std::nullopt;

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["jsonrpc"] = JSON_RPC_VERSION;
        if (id) {
            j["id"] = *id;
        } else {
            j["id"] = nullptr;
        }
        if (result) {
            j["result"] = *result;
        }
        if (error) {
            j["error"] = error->to_json();
        }
        return j;
    }

    static Response from_json(const nlohmann::json& j) {
        Response resp;
        if (j.contains("id") && !j["id"].is_null()) {
            resp.id = j["id"];
        }
        if (j.contains("result") && !j["result"].is_null()) {
            resp.result = j["result"];
        }
        if (j.contains("error") && !j["error"].is_null()) {
            resp.error = Error::from_json(j["error"]);
        }
        return resp;
    }

    static Response make_success(const nlohmann::json& result, std::optional<nlohmann::json> id = std::nullopt) {
        Response resp;
        resp.result = result;
        resp.id = std::move(id);
        return resp;
    }

    static Response make_error(const Error& error, std::optional<nlohmann::json> id = std::nullopt) {
        Response resp;
        resp.error = error;
        resp.id = std::move(id);
        return resp;
    }

    bool is_error() const { return error.has_value(); }
};

inline bool is_request(const nlohmann::json& j) {
    return j.contains("method");
}

inline bool is_response(const nlohmann::json& j) {
    return j.contains("result") || j.contains("error");
}

inline bool is_notification(const nlohmann::json& j) {
    return j.contains("method") && (!j.contains("id") || j["id"].is_null());
}

} // namespace jsonrpc
} // namespace agent