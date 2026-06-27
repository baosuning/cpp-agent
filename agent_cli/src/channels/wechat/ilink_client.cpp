// agent_cli/src/channels/ilink_client.cpp
// iLink Bot API 协议封装实现

#include "ilink_client.h"
#include <util/crypto_utils.h>
#include <util/utf8_utils.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>
#include <fstream>
#include <cstdint>

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
        std::string clean_body = agent::llm::sanitize_utf8_string(resp.body);
        return json::parse(clean_body);
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
        std::string clean_body = agent::llm::sanitize_utf8_string(resp.body);
        return json::parse(clean_body);
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
                    } else if (item.type == 2 && item_json.contains("image_item")) {
                        const auto& img = item_json.at("image_item");
                        // iLink 图片消息结构：
                        //   aeskey (根级, hex)              — AES-128 密钥
                        //   media.encrypt_query_param       — CDN 加密参数
                        //   media.full_url                  — 完整 CDN 下载 URL
                        //   thumb_width/thumb_height        — 缩略图尺寸
                        item.image_aes_key = img.value("aeskey", "");
                        item.image_width   = img.value("thumb_width", 0);
                        item.image_height  = img.value("thumb_height", 0);
                        if (img.contains("media")) {
                            const auto& media = img.at("media");
                            // 优先使用 full_url，download_image 会识别完整 URL
                            std::string full_url = media.value("full_url", "");
                            if (!full_url.empty()) {
                                item.image_encrypt_param = full_url;
                            } else {
                                item.image_encrypt_param = media.value("encrypt_query_param", "");
                            }
                        }
                        AGENT_LOG_DEBUG("IlinkClient") << "Image parsed: aeskey_len="
                            << (item.image_aes_key ? item.image_aes_key->size() : 0)
                            << " param_len=" << (item.image_encrypt_param ? item.image_encrypt_param->size() : 0)
                            << " " << item.image_width << "x" << item.image_height;
                    } else if (item.type == 2) {
                        AGENT_LOG_DEBUG("IlinkClient") << "type==2 but no image_item, item_json: "
                            << item_json.dump();
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
                            config_.quick_http_timeout_ms);
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
                            config_.quick_http_timeout_ms);
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
                            config_.quick_http_timeout_ms);
    if (!result) return false;

    int ret = result->value("ret", -1);
    if (ret != 0) {
        // typing 失败非关键，只 debug 记录
        AGENT_LOG_DEBUG("IlinkClient") << "sendtyping ret=" << ret;
        return false;
    }
    return true;
}

std::optional<UploadUrlResult> IlinkClient::get_upload_url(const std::string& content_type) {
    if (!is_authenticated()) {
        AGENT_LOG_ERROR("IlinkClient") << "get_upload_url called without auth";
        return std::nullopt;
    }

    json body;
    body["base_info"]    = {{"channel_version", config_.channel_version}};
    body["content_type"] = content_type;

    auto result = http_post("/ilink/bot/getuploadurl", body, build_auth_headers(),
                            config_.quick_http_timeout_ms);
    if (!result) return std::nullopt;

    // 检查 ret 字段（iLink API 错误码）
    if (result->contains("ret")) {
        int ret = result->at("ret").get<int>();
        if (ret != 0) {
            AGENT_LOG_ERROR("IlinkClient") << "getuploadurl ret=" << ret << ": " << result->dump();
            return std::nullopt;
        }
    }

    UploadUrlResult ur;
    ur.upload_url     = result->value("upload_url", "");
    ur.file_id        = result->value("file_id", "");
    ur.expire_seconds = result->value("expire_seconds", 0);

    if (ur.upload_url.empty() || ur.file_id.empty()) {
        AGENT_LOG_ERROR("IlinkClient") << "getuploadurl missing fields: " << result->dump();
        return std::nullopt;
    }

    AGENT_LOG_INFO("IlinkClient") << "getuploadurl: file_id=" << ur.file_id
        << ", expire_seconds=" << ur.expire_seconds;
    return ur;
}

std::vector<uint8_t> IlinkClient::download_image(const std::string& encrypt_query_param,
                                                   const std::string& aes_key_hex) {
    // 诊断日志：打印参数前 200 字符，帮助确认 CDN URL 格式
    std::string param_preview = encrypt_query_param.substr(0, std::min<size_t>(encrypt_query_param.size(), 200));
    std::string key_preview = aes_key_hex.substr(0, std::min<size_t>(aes_key_hex.size(), 32));
    AGENT_LOG_DEBUG("IlinkClient") << "download_image: param_preview=[" << param_preview << "]"
        << " key_preview=[" << key_preview << "]"
        << " param_len=" << encrypt_query_param.size()
        << " key_len=" << aes_key_hex.size();

    // 构造 CDN URL：encrypt_query_param 可能是 query string 或路径
    std::string cdn_url;
    if (encrypt_query_param.find("http://") == 0 || encrypt_query_param.find("https://") == 0) {
        // encrypt_query_param 本身就是完整 URL
        cdn_url = encrypt_query_param;
    } else if (encrypt_query_param.find("?") == 0) {
        // 以 ? 开头，直接拼接到 CDN 根路径
        cdn_url = "https://novac2c.cdn.weixin.qq.com/c2c" + encrypt_query_param;
    } else if (encrypt_query_param.find("/") == 0) {
        // 以 / 开头，作为路径拼接
        cdn_url = "https://novac2c.cdn.weixin.qq.com" + encrypt_query_param;
    } else {
        // 默认：作为 CDN 路径的一部分
        cdn_url = "https://novac2c.cdn.weixin.qq.com/c2c/" + encrypt_query_param;
    }
    AGENT_LOG_DEBUG("IlinkClient") << "download_image: cdn_url=" << cdn_url;

    auto resp = http_.get(cdn_url, {}, config_.quick_http_timeout_ms);
    if (resp.is_error || resp.status_code != 200) {
        AGENT_LOG_ERROR("IlinkClient") << "download_image failed: status=" << resp.status_code
            << " err=" << resp.error_message
            << " body_preview=[" << resp.body.substr(0, std::min<size_t>(resp.body.size(), 200)) << "]";
        return {};
    }

    // 大小限制检查（防止恶意超大图片导致内存耗尽）
    if (resp.body.size() > kMaxImageDownloadSize) {
        AGENT_LOG_ERROR("IlinkClient") << "download_image: image too large: " << resp.body.size()
            << " bytes (max=" << kMaxImageDownloadSize << ")";
        return {};
    }

    // 将响应体转为字节数组
    std::vector<uint8_t> encrypted_data(resp.body.begin(), resp.body.end());

    // AES 解密（捕获可能的异常：hex 解码失败、密文长度非法、PKCS7 去填充失败）
    try {
        auto key_bytes = agent::util::hex_decode(aes_key_hex);
        if (key_bytes.size() != 16) {
            AGENT_LOG_ERROR("IlinkClient") << "download_image: invalid aes_key length=" << key_bytes.size();
            return {};
        }

        auto decrypted = agent::util::aes_128_ecb_decrypt(encrypted_data, key_bytes);
        AGENT_LOG_DEBUG("IlinkClient") << "download_image: decrypted " << decrypted.size() << " bytes";
        return decrypted;
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("IlinkClient") << "download_image: AES decrypt failed: " << e.what();
        return {};
    }
}

bool IlinkClient::send_image(const std::string& to_user_id,
                              const std::string& file_path,
                              const std::string& context_token) {
    if (!is_authenticated()) {
        AGENT_LOG_ERROR("IlinkClient") << "send_image called without auth";
        return false;
    }

    // 1. 读取文件
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file) {
        AGENT_LOG_ERROR("IlinkClient") << "send_image: cannot open file: " << file_path;
        return false;
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> raw_data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(raw_data.data()), size)) {
        AGENT_LOG_ERROR("IlinkClient") << "send_image: failed to read file";
        return false;
    }

    // 2. 生成加密安全的 16 字节 AES key（使用 Windows CryptAPI）
    std::vector<uint8_t> aes_key;
    try {
        aes_key = agent::util::generate_aes_key();
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("IlinkClient") << "send_image: failed to generate AES key: " << e.what();
        return false;
    }

    // 3. 加密
    auto encrypted = agent::util::aes_128_ecb_encrypt(raw_data, aes_key);

    // 4. 获取上传地址
    // 根据文件扩展名确定 content_type（大小写不敏感）
    std::string content_type = "image/jpeg";
    size_t dot = file_path.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = file_path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == "png") content_type = "image/png";
        else if (ext == "gif") content_type = "image/gif";
        else if (ext == "webp") content_type = "image/webp";
    }

    auto upload_opt = get_upload_url(content_type);
    if (!upload_opt) {
        AGENT_LOG_ERROR("IlinkClient") << "send_image: get_upload_url failed";
        return false;
    }

    // 5. HTTP PUT 上传到 CDN
    std::string encrypted_body(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
    auto put_resp = http_.put(upload_opt->upload_url, encrypted_body,
                              {{"Content-Type", content_type}},
                              config_.quick_http_timeout_ms);
    if (put_resp.is_error || put_resp.status_code < 200 || put_resp.status_code >= 300) {
        AGENT_LOG_ERROR("IlinkClient") << "send_image: CDN PUT failed: status="
            << put_resp.status_code << " err=" << put_resp.error_message;
        return false;
    }

    // 6. 发送 sendmessage (type=2)
    // 复用已读入内存的 raw_data 计算 MD5，避免重复读取文件
    std::string md5 = agent::util::md5_hex(raw_data);
    std::string aes_key_hex = agent::util::hex_encode(aes_key);

    // 解析图片尺寸（支持 JPEG/PNG/GIF/WebP）
    auto img_size = agent::util::parse_image_size(raw_data);
    int width = img_size.width;
    int height = img_size.height;

    json msg;
    msg["client_id"]     = "agent_cli_" + std::to_string(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    msg["from_user_id"]  = "";
    msg["to_user_id"]    = to_user_id;
    msg["message_type"]  = 2;
    msg["message_state"] = 2;
    msg["context_token"] = context_token;
    msg["item_list"] = json::array({
        {{"type", 2}, {"image_item", {
            {"file_id", upload_opt->file_id},
            {"aes_key", aes_key_hex},
            {"md5", md5},
            {"width", width},
            {"height", height}
        }}}
    });

    json body;
    body["base_info"] = {{"channel_version", config_.channel_version}};
    body["msg"] = std::move(msg);

    auto result = http_post("/ilink/bot/sendmessage", body, build_auth_headers(),
                            config_.quick_http_timeout_ms);
    if (!result) {
        AGENT_LOG_ERROR("IlinkClient") << "send_image: sendmessage failed";
        return false;
    }

    if (result->contains("ret")) {
        int ret = result->at("ret").get<int>();
        if (ret != 0) {
            AGENT_LOG_ERROR("IlinkClient") << "send_image: sendmessage ret=" << ret;
            return false;
        }
    }
    AGENT_LOG_INFO("IlinkClient") << "send_image: success, file_id=" << upload_opt->file_id;
    return true;
}

} // namespace agent_cli::channels::wechat
