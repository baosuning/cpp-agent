#pragma once
// agent_cli/src/channels/ilink_client.h
// iLink Bot API 协议封装层：负责与 ilinkai.weixin.qq.com 的 HTTP/JSON 通信
// 纯协议封装，不涉及 Agent 业务逻辑

#include <util/i_http_client.h>
#include <util/log.h>
#include <util/base64.h>
#include <string>
#include <vector>
#include <optional>
#include <map>
#include <random>

namespace agent_cli::channels::wechat {

// iLink 客户端配置
struct IlinkConfig {
    std::string base_url              = "https://ilinkai.weixin.qq.com";
    int         longpoll_http_timeout_ms = 40000;  // HTTP 超时，略大于服务端 35s
    int         qrcode_poll_interval_ms  = 1000;   // 扫码状态轮询间隔
    int         qrcode_poll_timeout_ms   = 120000; // 扫码总超时 2 分钟
    std::string channel_version          = "1.0.2";
};

// 二维码信息（登录第一步）
struct QrCodeInfo {
    std::string qrcode_id;       // 用于查询扫码状态 ID
    std::string image_url;       // 二维码图片 URL（需 HTTP 下载得到 PNG 二进制）
};

// 登录结果（扫码确认后获得）
struct LoginResult {
    std::string bot_token;
    std::string base_url;        // 服务端返回的实际 baseurl
    std::string ilink_user_id;   // 当前登录微信用户的 ID，getconfig/sendmessage 需要
    std::string ilink_bot_id;    // Bot 账号 ID
};

// 入站消息项
struct InboundItem {
    int                         type = 0;   // 1=文本
    std::optional<std::string>  text;       // type=1 时填充
};

// 入站消息（用户发给 bot）
struct InboundMessage {
    std::string              from_user_id;   // @im.wechat
    std::string              to_user_id;     // @im.bot
    int                      message_type;   // 1=用户消息
    int                      message_state;
    std::string              context_token;  // 回复时必带
    std::vector<InboundItem> items;
};

// 长轮询结果
struct GetUpdatesResult {
    std::vector<InboundMessage> messages;
    std::string                 new_sync_buf;  // 新游标
};

// iLink 协议客户端
class IlinkClient {
public:
    explicit IlinkClient(agent::IHttpClient& http, IlinkConfig config = {});

    // ===== 登录流程 =====

    // 获取登录二维码（GET /ilink/bot/get_bot_qrcode?bot_type=3）
    std::optional<QrCodeInfo> get_login_qrcode();

    // 轮询扫码状态
    // 返回值：pending（等待扫码）/ scanned（已扫码未确认）→ 返回 nullopt
    //         confirmed（确认）→ 返回 LoginResult
    std::optional<LoginResult> poll_qrcode_status(const std::string& qrcode_id);

    // 设置凭据（从持久化加载后调用）
    void set_credentials(std::string bot_token, std::string base_url,
                         std::string ilink_user_id, std::string ilink_bot_id);

    bool is_authenticated() const { return !bot_token_.empty(); }
    const std::string& bot_token() const { return bot_token_; }

    // ===== 消息收发 =====

    // 长轮询收消息（阻塞，最多 longpoll_http_timeout_ms）
    std::optional<GetUpdatesResult> get_updates(const std::string& sync_buf);

    // 发送文本消息（message_type=2, message_state=2 FINISH）
    bool send_text(const std::string& to_user_id,
                   const std::string& text,
                   const std::string& context_token);

    // 获取 typing_ticket（sendtyping 前置，缓存结果）
    std::optional<std::string> get_typing_ticket();

    // 发送"正在输入"状态
    bool send_typing(const std::string& context_token,
                     const std::string& typing_ticket,
                     int status);

private:
    agent::IHttpClient&   http_;
    IlinkConfig           config_;
    std::string           bot_token_;
    std::string           effective_base_url_;
    std::string           ilink_user_id_;
    std::string           ilink_bot_id_;
    std::optional<std::string> cached_typing_ticket_;
    std::mt19937          rng_{std::random_device{}()};

    // 生成 X-WECHAT-UIN 头：随机 uint32 → 十进制字符串 → base64
    std::string generate_wechat_uin();

    // 构造带鉴权的请求头
    std::map<std::string, std::string> build_auth_headers();

    // 构造通用请求头（无 Authorization，用于登录前请求）
    std::map<std::string, std::string> build_common_headers();

    // GET 请求辅助，返回解析后的 JSON；失败返回 nullopt
    std::optional<nlohmann::json> http_get(const std::string& path_and_query,
                                           const std::map<std::string, std::string>& headers);
    // POST 请求辅助
    std::optional<nlohmann::json> http_post(const std::string& path,
                                            const nlohmann::json& body,
                                            const std::map<std::string, std::string>& headers,
                                            int timeout_ms);
};

} // namespace agent_cli::channels::wechat
