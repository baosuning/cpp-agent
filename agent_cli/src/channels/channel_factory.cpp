// agent_cli/src/channels/channel_factory.cpp
// 渠道工厂实现
// 新增平台步骤：
//   1. 在 src/channels/<platform>/ 下实现 IChannel 子类
//   2. 在此处 include 并添加 if 分支
//   3. 在 channel_factory.h 的 platform 注释中补充平台名

#include "channel_factory.h"
#include "wechat/ilink_client.h"
#include "wechat/wechat_channel.h"

// 未来扩展（实现后取消注释）：
// #include "feishu/feishu_channel.h"   // 飞书
// #include "wecom/wecom_channel.h"     // 企业微信
// #include "qq/qq_channel.h"           // QQ

namespace agent_cli::channels {

std::unique_ptr<IChannel> create_channel(const std::string& platform,
                                         agent::IHttpClient& http,
                                         const std::string& data_dir) {
    if (platform == "wechat") {
        // iLink Bot API（个人微信）
        auto ilink = std::make_unique<wechat::IlinkClient>(http);
        wechat::WeChatChannelConfig cfg;
        cfg.session_file  = data_dir + "wechat/session.json";
        cfg.sync_buf_file = data_dir + "wechat/sync_buf.txt";
        cfg.qrcode_file   = data_dir + "wechat/qrcode.png";
        // IlinkClient 所有权转移给 WeChatChannel
        return std::make_unique<wechat::WeChatChannel>(std::move(ilink), std::move(cfg));
    }

    // TODO: 飞书渠道（开放平台机器人，Webhook + 事件订阅）
    // if (platform == "feishu") {
    //     return std::make_unique<feishu::FeishuChannel>(http, data_dir + "feishu/");
    // }

    // TODO: 企业微信渠道（自建应用，回调 URL + 主动消息 API）
    // if (platform == "wecom") {
    //     return std::make_unique<wecom::WecomChannel>(http, data_dir + "wecom/");
    // }

    // TODO: QQ 渠道（QQ 机器人开放接口）
    // if (platform == "qq") {
    //     return std::make_unique<qq::QQChannel>(http, data_dir + "qq/");
    // }

    return nullptr;
}

} // namespace agent_cli::channels
