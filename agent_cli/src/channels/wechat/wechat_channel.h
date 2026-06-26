#pragma once
// agent_cli/src/channels/wechat/wechat_channel.h
// 微信渠道编排层：连接 IlinkClient（协议）和 Agent（业务）
// 管理登录、长轮询、消息处理、typing、持久化

#include "ilink_client.h"
#include "channels/channel.h"
#include <agent/agent.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#include <cstdint>

namespace agent_cli::channels::wechat {

// 渠道配置
struct WeChatChannelConfig {
    std::string session_file    = "data/wechat/session.json";
    std::string sync_buf_file   = "data/wechat/sync_buf.txt";
    std::string qrcode_file     = "data/wechat/qrcode.png";
    bool        auto_open_qrcode = true;     // 自动用系统默认程序打开二维码图片
    bool        enable_typing   = true;      // 处理期间发送 typing
    int         typing_interval_ms = 3000;   // typing 发送间隔
    int         reconnect_delay_ms = 5000;   // 长轮询失败后重连间隔
};

// 微信渠道（实现 IChannel 统一接口）
class WeChatChannel : public agent_cli::channels::IChannel {
public:
    // client 所有权转移给 WeChatChannel
    WeChatChannel(std::unique_ptr<IlinkClient> client, WeChatChannelConfig config = {});
    ~WeChatChannel();

    // IChannel 接口
    bool start(agent::Agent& agent) override;
    void stop() override;
    bool is_running() const override { return running_.load(); }

private:
    std::unique_ptr<IlinkClient> client_;
    WeChatChannelConfig    config_;
    agent::Agent*          agent_ = nullptr;

    std::atomic<bool>      running_{false};
    std::thread            poll_thread_;
    std::thread            process_thread_;

    // 消息队列（长轮询线程 → 处理线程）
    std::queue<InboundMessage> msg_queue_;
    std::mutex               queue_mutex_;
    std::condition_variable  queue_cv_;

    // 当前处理中的消息上下文（用于 reply 和 typing）
    std::mutex              current_msg_mutex_;
    std::string             current_context_token_;
    std::string             current_from_user_id_;
    std::condition_variable state_cv_;         // 等待 Agent 到达 Idle/Error
    std::atomic<bool>       agent_idle_{false};

    // 持久化
    bool save_session(const LoginResult& login);
    std::optional<LoginResult> load_session();
    bool save_sync_buf(const std::string& buf);
    std::string load_sync_buf();

    // 登录流程
    bool do_login();
    bool save_and_show_qrcode(const std::string& image_url);
    void on_login_success();

    // Windows 下记录二维码图片查看器进程 ID，登录成功后关闭
    uint32_t qrcode_process_id_ = 0;

    // 长轮询主循环
    void poll_loop();
    // 消息处理主循环
    void process_loop();
    // 处理单条消息
    void handle_message(const InboundMessage& msg);
    // Typing 发送（处理期间，阻塞直到 agent_idle）
    void run_typing_during_processing(const std::string& context_token);
    // 回复消息
    void reply(const std::string& text);
};

} // namespace agent_cli::channels::wechat
