// agent_cli/src/channels/ilink_client.cpp
// iLink Bot API 协议封装实现

#include "ilink_client.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

namespace agent_cli::channels::wechat {

using json = nlohmann::json;

IlinkClient::IlinkClient(agent::IHttpClient& http, IlinkConfig config)
    : http_(http), config_(std::move(config)) {
    effective_base_url_ = config_.base_url;
}

std::string IlinkClient::generate_wechat_uin() {
    // 随机 uint32 → 十进制字符串 → base64
    uint32_t r = rng_();
    std::string num_str = std::to_string(r);
    return agent::util::base64_encode(num_str);
}

std::map<std::string, std::string> IlinkClient::build_common_headers() {
    return {
        {"Content-Type", "application/json"},
        {"AuthorizationType", "ilink_bot_token"},
        {"X-WECHAT-UIN", generate_wechat_uin()},
    };
}

std::map<std::string, std::string> IlinkClient::build_auth_headers() {
    auto headers = build_common_headers();
    headers["Authorization"] = "Bearer " + bot_token_;
    return headers;
}

void IlinkClient::set_credentials(std::string bot_token, std::string base_url,
                                   std::string ilink_user_id, std::string ilink_bot_id) {
    bot_token_ = std::move(bot_token);
    if (!base_url.empty()) {
        effective_base_url_ = base_url;
    }
    ilink_user_id_ = std::move(ilink_user_id);
    ilink_bot_id_  = std::move(ilink_bot_id);
    AGENT_LOG_INFO("IlinkClient") << "Credentials set, base_url=" << effective_base_url_
        << ", ilink_user_id=" << ilink_user_id_;
}

std::optional<nlohmann::json> IlinkClient::http_get(
    const std::string& path_and_query,
    const std::map<std::string, std::string>& headers) {
    std::string url = effective_base_url_ + path_and_query;
    auto resp = http_.get(url, headers, config_.longpoll_http_timeout_ms);
    if (resp.is_error || resp.status_code < 200 || resp.status_code >= 300) {
        AGENT_LOG_ERROR("IlinkClient") << "GET " << path_and_query
            << " failed: status=" << resp.status_code
            << " err=" << resp.error_message;
        return std::nullopt;
    }
    try {
        return json::parse(resp.body);
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("IlinkClient") << "GET " << path_and_query
            << " JSON parse failed: " << e.what();
        return std::nullopt;
    }
}

std::optional<nlohmann::json> IlinkClient::http_post(
    const std::string& path,
    const nlohmann::json& body,
    const std::map<std::string, std::string>& headers,
    int timeout_ms) {
    std::string url = effective_base_url_ + path;
    auto resp = http_.post(url, body.dump(), headers, timeout_ms);
    if (resp.is_error || resp.status_code < 200 || resp.status_code >= 300) {
        AGENT_LOG_ERROR("IlinkClient") << "POST " << path
            << " failed: status=" << resp.status_code
            << " err=" << resp.error_message;
        return std::nullopt;
    }
    try {
        return json::parse(resp.body);
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("IlinkClient") << "POST " << path
            << " JSON parse failed: " << e.what();
        return std::nullopt;
    }
}

// ===== 登录流程 =====

std::optional<QrCodeInfo> IlinkClient::get_login_qrcode() {
    auto headers = build_common_headers();
    auto result = http_get("/ilink/bot/get_bot_qrcode?bot_type=3", headers);
    if (!result) return std::nullopt;

    QrCodeInfo info;
    info.qrcode_id = result->value("qrcode", "");
    info.image_url = result->value("qrcode_img_content", "");

    if (info.qrcode_id.empty() || info.image_url.empty()) {
        AGENT_LOG_ERROR("IlinkClient") << "Login qrcode response missing fields: "
            << result->dump();
        return std::nullopt;
    }
    AGENT_LOG_INFO("IlinkClient") << "Got login qrcode, id=" << info.qrcode_id
        << ", image_url=" << info.image_url;
    return info;
}

std::optional<LoginResult> IlinkClient::poll_qrcode_status(const std::string& qrcode_id) {
    auto headers = build_common_headers();
    auto result = http_get("/ilink/bot/get_qrcode_status?qrcode=" + qrcode_id, headers);
    if (!result) return std::nullopt;

    AGENT_LOG_INFO("IlinkClient") << "get_qrcode_status response: " << result->dump();

    std::string status = result->value("status", "");
    if (status == "confirmed") {
        // 凭证可能在根级或 data.credentials 嵌套路径下，两种都尝试
        LoginResult lr;
        lr.bot_token      = result->value("bot_token", "");
        lr.base_url       = result->value("baseurl", "");
        lr.ilink_user_id  = result->value("ilink_user_id", "");
        lr.ilink_bot_id   = result->value("ilink_bot_id", "");

        if (lr.bot_token.empty() && result->contains("data")) {
            const auto& data = result->at("data");
            lr.bot_token      = data.value("bot_token", "");
            lr.base_url       = data.value("baseurl", "");
            lr.ilink_user_id  = data.value("ilink_user_id", "");
            lr.ilink_bot_id   = data.value("ilink_bot_id", "");
            if (lr.bot_token.empty() && data.contains("credentials")) {
                const auto& cred = data.at("credentials");
                lr.bot_token      = cred.value("bot_token", "");
                lr.base_url       = cred.value("baseurl", "");
                lr.ilink_user_id  = cred.value("ilink_user_id", "");
                lr.ilink_bot_id   = cred.value("ilink_bot_id", "");
            }
        }

        if (lr.bot_token.empty()) {
            AGENT_LOG_ERROR("IlinkClient") << "Confirmed but bot_token empty: " << result->dump();
            return std::nullopt;
        }
        AGENT_LOG_INFO("IlinkClient") << "Login confirmed, base_url=" << lr.base_url
            << ", ilink_user_id=" << lr.ilink_user_id << ", ilink_bot_id=" << lr.ilink_bot_id;
        return lr;
    }
    if (status == "expired") {
        AGENT_LOG_WARN("IlinkClient") << "QR code expired, need re-login";
        // 用特殊标记区分"过期"和"等待中"，让 do_login 能重新获取二维码
        LoginResult expired_marker;
        expired_marker.bot_token = "__EXPIRED__";
        return expired_marker;
    }
    if (status == "scaned" || status == "scanned") {
        AGENT_LOG_INFO("IlinkClient") << "QR code scanned, waiting for confirm...";
    }
    // wait / scaned / 其他 → 未确认，返回 nullopt
    return std::nullopt;
}

// ===== 消息收发 =====

std::optional<GetUpdatesResult> IlinkClient::get_updates(const std::string& sync_buf) {
    if (!is_authenticated()) {
        AGENT_LOG_ERROR("IlinkClient") << "get_updates called without auth";
        return std::nullopt;
    }

    json body;
    body["base_info"] = {{"channel_version", config_.channel_version}};
    body["get_updates_buf"] = sync_buf;

    auto result = http_post("/ilink/bot/getupdates", body, build_auth_headers(),
                            config_.longpoll_http_timeout_ms);
    if (!result) return std::nullopt;

    // iLink getupdates 接口经常返回 ret=-1，但业务数据完全正常（无论 msgs 是否为空），
    // 因此 ret 字段仅用于调试记录，不影响流程；优先使用 sync_buf 作为下一次游标
    int ret = result->value("ret", -1);
    AGENT_LOG_DEBUG("IlinkClient") << "getupdates ret=" << ret
        << ", msgs=" << (result->contains("msgs") ? result->at("msgs").size() : 0);

    GetUpdatesResult gur;
    if (result->contains("sync_buf") && !result->at("sync_buf").empty()) {
        gur.new_sync_buf = result->at("sync_buf").get<std::string>();
    } else {
        gur.new_sync_buf = result->value("get_updates_buf", sync_buf);
    }

    if (result->contains("msgs") && result->at("msgs").is_array()) {
        for (const auto& msg_json : result->at("msgs")) {
            InboundMessage msg;
            msg.from_user_id = msg_json.value("from_user_id", "");
            msg.to_user_id = msg_json.value("to_user_id", "");
            msg.message_type = msg_json.value("message_type", 0);
            msg.message_state = msg_json.value("message_state", 0);
            msg.context_token = msg_json.value("context_token", "");

            if (msg_json.contains("item_list") && msg_json.at("item_list").is_array()) {
                for (const auto& item_json : msg_json.at("item_list")) {
                    InboundItem item;
                    item.type = item_json.value("type", 0);
                    if (item.type == 1 && item_json.contains("text_item")) {
                        item.text = item_json.at("text_item").value("text", "");
                    }
                    msg.items.push_back(std::move(item));
                }
            }
            gur.messages.push_back(std::move(msg));
        }
    }
    return gur;
}

bool IlinkClient::send_text(const std::string& to_user_id,
                            const std::string& text,
                            const std::string& context_token) {
    if (!is_authenticated()) {
        AGENT_LOG_ERROR("IlinkClient") << "send_text called without auth";
        return false;
    }

    json msg;
    msg["client_id"]     = "agent_cli_" + std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    msg["from_user_id"]  = "";        // Bot 发出，固定空字符串
    msg["to_user_id"]    = to_user_id;
    msg["message_type"]  = 2;         // BOT 发出
    msg["message_state"] = 2;         // FINISH
    msg["context_token"] = context_token;
    msg["item_list"] = json::array({
        {{"type", 1}, {"text_item", {{"text", text}}}}
    });

    json body;
    body["base_info"] = {{"channel_version", config_.channel_version}};
    body["msg"] = std::move(msg);

    auto result = http_post("/ilink/bot/sendmessage", body, build_auth_headers(),
                            config_.longpoll_http_timeout_ms);
    if (!result) return false;

    // iLink sendmessage 成功投递后可能返回空对象 {} 或不包含 ret 字段，
    // 此时只要 HTTP 200 就视为成功；只有显式 ret != 0 才失败
    if (result->contains("ret")) {
        int ret = result->at("ret").get<int>();
        if (ret != 0) {
            AGENT_LOG_ERROR("IlinkClient") << "sendmessage ret=" << ret << ": " << result->dump()
                << " body=" << body.dump();
            return false;
        }
    }
    return true;
}

std::optional<std::string> IlinkClient::get_typing_ticket() {
    if (cached_typing_ticket_) return cached_typing_ticket_;
    if (!is_authenticated()) return std::nullopt;

    json body;
    body["base_info"] = {{"channel_version", config_.channel_version}};
    body["ilink_user_id"] = ilink_user_id_;
    auto result = http_post("/ilink/bot/getconfig", body, build_auth_headers(),
                            config_.longpoll_http_timeout_ms);
    if (!result) return std::nullopt;

    int ret = result->value("ret", -1);
    if (ret != 0) {
        AGENT_LOG_WARN("IlinkClient") << "getconfig ret=" << ret << ": " << result->dump();
        return std::nullopt;
    }

    // typing_ticket 可能在不同层级，尝试多个字段名
    std::string ticket = result->value("typing_ticket", "");
    if (ticket.empty() && result->contains("config")) {
        ticket = result->at("config").value("typing_ticket", "");
    }
    if (ticket.empty()) {
        AGENT_LOG_WARN("IlinkClient") << "getconfig: typing_ticket not found: " << result->dump();
        return std::nullopt;
    }
    cached_typing_ticket_ = ticket;
    AGENT_LOG_INFO("IlinkClient") << "Got typing_ticket (len=" << ticket.size() << ")";
    return cached_typing_ticket_;
}

bool IlinkClient::send_typing(const std::string& context_token,
                              const std::string& typing_ticket,
                              int status) {
    if (!is_authenticated()) return false;

    json body;
    body["base_info"] = {{"channel_version", config_.channel_version}};
    body["ilink_user_id"] = ilink_user_id_;
    body["context_token"] = context_token;
    body["typing_ticket"] = typing_ticket;
    body["status"] = status;  // 1 = 开始输入, 2 = 结束输入

    auto result = http_post("/ilink/bot/sendtyping", body, build_auth_headers(),
                            config_.longpoll_http_timeout_ms);
    if (!result) return false;

    int ret = result->value("ret", -1);
    if (ret != 0) {
        // typing 失败非关键，只 debug 记录
        AGENT_LOG_DEBUG("IlinkClient") << "sendtyping ret=" << ret;
        return false;
    }
    return true;
}

} // namespace agent_cli::channels::wechat
