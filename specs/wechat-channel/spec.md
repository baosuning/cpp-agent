# WeChat Channel (iLink Bot API) 设计规格说明

> **Status: 设计中，待审核**
>
> 基于 2026-03-22 腾讯正式发布的 iLink Bot API，为 agent_cli 增加通过微信私聊操控 Agent 的能力。
> 第一版仅支持**单用户私聊文本**，不做群聊和媒体消息。

## 1. 概述

### 1.1 目标

- 在 agent_cli 中新增 `--wechat` 启动模式，扫码登录后通过微信私聊与 Agent 交互
- 完整使用 C++ 实现 iLink HTTP/JSON 协议，不依赖 Node.js/Python 桥接
- 复用现有 Agent 引擎（ReAct/Plan-and-Execute/Reflection）、工具系统、MCP、记忆等能力
- 单用户私聊场景：一个 Agent 实例服务一个微信用户

### 1.2 非目标（第一版不做）

- 群聊（iLink bot 身份群聊投递不稳定，社区反馈多数账号收不到群消息）
- 媒体消息（图片/语音/文件/视频，涉及 AES-128-ECB + CDN，复杂度高）
- 多用户并发会话（每用户独立 ContextManager）
- 消息历史拉取（iLink 只有游标机制，无历史 API）
- 流式回复（message_state 流式状态，第一版用 FINISH 一次性回复）

### 1.3 合规边界

基于《微信ClawBot功能使用条款》：
- 腾讯只是"消息管道"，不存储内容，不对 AI 输出负责
- 腾讯保留限速/封禁/过滤/终止服务的权利
- 收集 IP/操作/设备日志用于安全审计
- **不应将核心业务完全依赖此 API**，需有降级方案（如 CLI 模式）

## 2. iLink 协议规范摘要

### 2.1 服务地址

| 用途 | 域名 |
|------|------|
| API 主域名 | `https://ilinkai.weixin.qq.com` |
| CDN（媒体，本期不用） | `https://novac2c.cdn.weixin.qq.com/c2c` |

协议：标准 HTTP/JSON，无需 SDK。

### 2.2 鉴权

**登录流程**：
```
1. GET /ilink/bot/get_bot_qrcode?bot_type=3
   → { qrcode, qrcode_img_content }   // qrcode=状态查询ID, img_content=base64 PNG

2. 轮询 GET /ilink/bot/get_qrcode_status?qrcode=<qrcode>
   → { status: "confirmed", bot_token, baseurl }  // 扫码确认后获得 token
```

**后续请求固定请求头**：
```
Content-Type: application/json
AuthorizationType: ilink_bot_token
X-WECHAT-UIN: base64(String(randomUint32()))   // 每次随机，防重放
Authorization: Bearer ${bot_token}
```

`X-WECHAT-UIN` 生成：随机 uint32 → 十进制字符串 → base64 编码。每次请求都变。

### 2.3 API 列表

| Endpoint | Method | 功能 | 第一版 |
|----------|--------|------|--------|
| `/ilink/bot/get_bot_qrcode` | GET | 获取登录二维码（`?bot_type=3`） | ✅ |
| `/ilink/bot/get_qrcode_status` | GET | 轮询扫码状态（`?qrcode=xxx`） | ✅ |
| `/ilink/bot/getupdates` | POST | 长轮询收消息（hold 35s） | ✅ |
| `/ilink/bot/sendmessage` | POST | 发送消息 | ✅ |
| `/ilink/bot/sendtyping` | POST | 发送"正在输入"状态 | ✅ |
| `/ilink/bot/getconfig` | POST | 获取 typing_ticket（sendtyping 前置） | ✅ |
| `/ilink/bot/getuploadurl` | POST | 获取 CDN 上传地址 | ❌ |

### 2.4 长轮询收消息

**请求**：
```json
POST /ilink/bot/getupdates
{
  "get_updates_buf": "<上次返回的游标，首次为空字符串>",
  "base_info": { "channel_version": "1.0.2" }
}
```

**响应**（服务器 hold 最多 35 秒）：
```json
{
  "ret": 0,
  "msgs": [ ...WeixinMessage[] ],
  "get_updates_buf": "<新游标，下次请求带上>",
  "longpolling_timeout_ms": 35000
}
```

**关键**：`get_updates_buf` 是游标，必须每次更新并持久化，否则重启会重复收到旧消息。

### 2.5 消息结构

**Inbound 消息**（用户发给 bot）：
```json
{
  "from_user_id": "o9cq800kum_xxx@im.wechat",
  "to_user_id": "e06c1ceea05e@im.bot",
  "message_type": 1,
  "message_state": 2,
  "context_token": "AARzJWAFAAABAAAAAAAp...",
  "item_list": [
    { "type": 1, "text_item": { "text": "你好" } }
  ]
}
```

**ID 规律**：
- 用户 ID：`xxx@im.wechat`
- Bot ID：`xxx@im.bot`

**item_list[].type**：1=文本, 2=图片, 3=语音, 4=文件, 5=视频（第一版只处理 type=1）

### 2.6 回复机制（关键）

**发送消息请求**：
```json
POST /ilink/bot/sendmessage
{
  "msg": {
    "to_user_id": "o9cq800kum_xxx@im.wechat",
    "message_type": 2,
    "message_state": 2,
    "context_token": "<从 inbound 消息原样取>",
    "item_list": [
      { "type": 1, "text_item": { "text": "你好！" } }
    ]
  }
}
```

**关键约束**：
- `context_token` 必须从 inbound 消息原样带回，否则消息不会关联到正确对话窗口
- `message_type=2`（BOT 发出），`message_state=2`（FINISH 完整消息）
- 第一版只发文本（type=1）

## 3. 架构设计

### 3.1 整体架构

```
┌──────────────────────────────────────────────────────────┐
│                    agent_cli (main.cpp)                   │
│  --wechat 模式：创建 WeChatChannel + Agent                │
├──────────────────────────────────────────────────────────┤
│              WeChatChannel (渠道层)                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐   │
│  │ 登录流程      │  │ 长轮询线程    │  │ Typing 线程  │   │
│  │ (扫码+持久化) │  │ (收消息分发)  │  │ (处理期间)   │   │
│  └──────────────┘  └──────────────┘  └──────────────┘   │
│         │                  │                  │           │
│         ▼                  ▼                  ▼           │
│  ┌─────────────────────────────────────────────────┐    │
│  │            IlinkClient (协议封装层)               │    │
│  │  get_qrcode / poll_status / get_updates /        │    │
│  │  send_text / send_typing / get_config            │    │
│  └─────────────────────────────────────────────────┘    │
│                          │                               │
│                          ▼                               │
│              IHttpClient (WinHttpClient, HTTPS)           │
├──────────────────────────────────────────────────────────┤
│              iLink 服务器 (ilinkai.weixin.qq.com)         │
└──────────────────────────────────────────────────────────┘
                          │
                          ▼ (收到消息后)
┌──────────────────────────────────────────────────────────┐
│              Agent 引擎 (复用现有)                         │
│  submit_input() → ReAct/PE/Reflection → on_output_ready  │
│  工具/MCP/记忆/人格 全部复用                               │
└──────────────────────────────────────────────────────────┘
```

### 3.2 线程模型

单用户场景采用**串行处理 + 三线程**模型：

| 线程 | 职责 | 阻塞点 |
|------|------|--------|
| 主线程 | CLI 启动、配置加载、创建 Agent 和 Channel、等待退出信号 | 等待 stop 信号 |
| 长轮询线程 | 循环调用 `getupdates`，收到消息放入队列 | HTTP 长轮询 35s |
| 处理线程 | 从队列取消息 → `submit_input` → 等 `on_output_ready` → `reply` | Agent 处理耗时 |
| Typing 线程 | Agent 处理期间每 3s 发一次 `sendtyping` | sleep 3s 循环 |

**为何串行处理**：单用户场景，用户发完一条消息会等回复再发下一条。串行避免并发上下文混乱，且实现简单。若用户在 Agent 处理期间连发多条，消息进队列排队处理。

**消息队列**：`std::queue<InboundMessage>` + `std::mutex` + `std::condition_variable`，处理线程阻塞等待。

### 3.3 会话状态（单用户简化）

单用户场景下，只维护一个"当前活跃会话"：
- 收到消息时，记录该消息的 `context_token` 和 `from_user_id`
- Agent 输出时，用最近记录的 `context_token` 回复
- 不需要多用户会话池

## 4. 模块设计

### 4.1 文件结构

```
agent_cli/
├── src/
│   ├── channels/
│   │   ├── ilink_client.h          # iLink HTTP API 协议封装
│   │   ├── ilink_client.cpp
│   │   ├── wechat_channel.h        # 渠道层（登录/轮询/回复编排）
│   │   └── wechat_channel.cpp
│   └── ...
├── config/
│   └── wechat.json.example         # 配置示例（复制为 wechat.json 使用）
└── data/
    └── wechat/                     # 运行时数据（.gitignore）
        ├── session.json            # bot_token 持久化
        ├── sync_buf.txt            # 长轮询游标
        └── qrcode.png              # 登录二维码图片（临时）
```

### 4.2 IlinkClient（协议封装层）

职责：纯协议封装，不涉及业务逻辑，不涉及 Agent。

```cpp
// agent_cli/src/channels/ilink_client.h
#pragma once
#include <util/i_http_client.h>
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace agent_cli::channels::wechat {

// iLink 客户端配置
struct IlinkConfig {
    std::string base_url = "https://ilinkai.weixin.qq.com";
    int         longpoll_http_timeout_ms = 40000;  // HTTP 超时，略大于 35s
    int         qrcode_poll_interval_ms  = 1000;   // 扫码状态轮询间隔
    int         qrcode_poll_timeout_ms   = 120000; // 扫码总超时 2 分钟
    std::string channel_version          = "1.0.2";
};

// 二维码信息
struct QrCodeInfo {
    std::string qrcode_id;            // 用于查询状态 ID
    std::string image_base64;         // PNG 图片 base64（不含 data: 前缀）
};

// 登录结果
struct LoginResult {
    std::string bot_token;
    std::string base_url;             // 可能和默认 base_url 不同
    std::string bot_user_id;          // @im.bot，从首次 getupdates 推断
};

// 入站消息项
struct InboundItem {
    int                      type = 0;        // 1=文本
    std::optional<std::string> text;          // type=1 时填充
    // 后续版本扩展图片/语音/文件字段
};

// 入站消息
struct InboundMessage {
    std::string              from_user_id;    // @im.wechat
    std::string              to_user_id;      // @im.bot
    int                      message_type;    // 1=用户消息
    int                      message_state;
    std::string              context_token;   // 回复时必带
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

    // 轮询扫码状态，返回 LoginResult（扫码确认后）
    // 状态值：pending / scanned / confirmed
    std::optional<LoginResult> poll_qrcode_status(const std::string& qrcode_id);

    // 设置凭据（从持久化加载后调用）
    void set_credentials(std::string bot_token, std::string base_url);

    bool is_authenticated() const;

    // ===== 消息收发 =====

    // 长轮询收消息（阻塞，最多 longpoll_http_timeout_ms）
    // 网络错误返回 nullopt；ret!=0 返回 nullopt 并记日志
    std::optional<GetUpdatesResult> get_updates(const std::string& sync_buf);

    // 发送文本消息（message_type=2, message_state=2）
    bool send_text(const std::string& to_user_id,
                   const std::string& text,
                   const std::string& context_token);

    // 获取 typing_ticket（sendtyping 前置，缓存结果）
    std::optional<std::string> get_typing_ticket();

    // 发送"正在输入"状态
    bool send_typing(const std::string& context_token,
                     const std::string& typing_ticket);

private:
    agent::IHttpClient&   http_;
    IlinkConfig           config_;
    std::string           bot_token_;
    std::string           effective_base_url_;
    std::optional<std::string> cached_typing_ticket_;

    // 生成 X-WECHAT-UIN 头：随机 uint32 → 十进制字符串 → base64
    std::string generate_wechat_uin() const;

    // 构造带鉴权的请求头
    std::map<std::string, std::string> build_auth_headers() const;

    // GET 请求辅助
    nlohmann::json get(const std::string& path_and_query);
    // POST 请求辅助
    nlohmann::json post(const std::string& path, const nlohmann::json& body);
};

} // namespace
```

### 4.3 WeChatChannel（渠道编排层）

职责：连接 IlinkClient 和 Agent，管理线程、持久化、登录流程。

```cpp
// agent_cli/src/channels/wechat_channel.h
#pragma once
#include "ilink_client.h"
#include <agent/agent.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

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
    int         max_reconnect_attempts = -1; // -1=无限重试
};

// 微信渠道
class WeChatChannel {
public:
    WeChatChannel(IlinkClient& client, WeChatChannelConfig config = {});

    ~WeChatChannel();

    // 启动渠道：尝试加载已保存的 session，失败则走登录流程
    // 登录成功后启动长轮询线程和消息处理线程
    // agent 已由调用方创建并配置好工具/MCP/记忆等
    bool start(agent::Agent& agent);

    void stop();

    bool is_running() const;

private:
    IlinkClient&           client_;
    WeChatChannelConfig    config_;
    agent::Agent*          agent_ = nullptr;

    std::atomic<bool>      running_{false};
    std::thread            poll_thread_;
    std::thread            process_thread_;
    std::thread            typing_thread_;

    // 消息队列（长轮询线程 → 处理线程）
    std::queue<InboundMessage> msg_queue_;
    std::mutex               queue_mutex_;
    std::condition_variable  queue_cv_;

    // 当前处理中的消息（用于 reply 和 typing）
    std::mutex              current_msg_mutex_;
    std::string             current_context_token_;
    std::string             current_from_user_id_;
    std::condition_variable output_cv_;        // 等待 Agent 输出
    std::string             current_output_;
    bool                    output_ready_ = false;

    // 持久化
    bool save_session(const LoginResult& login);
    std::optional<LoginResult> load_session();
    bool save_sync_buf(const std::string& buf);
    std::string load_sync_buf();

    // 登录流程
    bool do_login();

    // 二维码显示：保存 PNG + 系统打开 + 控制台提示
    bool save_and_show_qrcode(const std::string& image_base64);

    // 长轮询主循环
    void poll_loop();

    // 消息处理主循环
    void process_loop();

    // Typing 发送循环（处理期间运行）
    void typing_loop();

    // 处理单条消息
    void handle_message(const InboundMessage& msg);

    // 回复消息
    void reply(const std::string& text);
};

} // namespace
```

### 4.4 Base64 工具

项目当前没有 base64 实现，需新增。iLink 用到两处：
- `X-WECHAT-UIN` 生成（编码 uint32 字符串）
- 二维码 PNG 图片保存（解码 base64 → 二进制）

```cpp
// agent_lib/include/util/base64.h
#pragma once
#include <string>
#include <vector>

namespace agent::util {

// 编码
std::string base64_encode(const std::string& data);
std::string base64_encode(const std::vector<unsigned char>& data);

// 解码
std::vector<unsigned char> base64_decode(const std::string& encoded);

// 解码为字符串（非二进制场景）
std::string base64_decode_to_string(const std::string& encoded);

} // namespace
```

实现放在 `agent_lib/src/util/base64.cpp`，无外部依赖，纯标准库实现。

## 5. 关键流程

### 5.1 启动流程

```
main(--wechat)
  │
  ├── 解析 wechat.json 配置
  ├── 创建 WinHttpClient
  ├── 创建 IlinkClient(http)
  ├── 创建 WeChatChannel(ilink_client, config)
  │
  ├── 创建 Agent (create_react/create_plan_execute/create_reflection)
  │   ├── 注册内置工具（fs/web/cmd/script）
  │   ├── 注册 MCP
  │   ├── 注册记忆
  │   └── set_on_output_ready(callback)  ← 关键：回调里通知 channel
  │
  ├── channel.start(agent)
  │   ├── 尝试 load_session()
  │   │   ├── 有有效 session → set_credentials() → 跳过登录
  │   │   └── 无 session → do_login()
  │   │       ├── get_login_qrcode()
  │   │       ├── save_and_show_qrcode()  ← 保存PNG+系统打开
  │   │       ├── 循环 poll_qrcode_status() 直到 confirmed
  │   │       └── save_session()
  │   │
  │   ├── 启动 poll_thread_  (长轮询收消息)
  │   └── 启动 process_thread_ (消息处理)
  │
  └── 主线程等待 stop 信号（Ctrl+C 或 quit）
```

### 5.2 消息收发流程

```
[长轮询线程]                    [处理线程]                    [Typing线程]
     │                              │                              │
     ├── get_updates(sync_buf)      │                              │
     │   (阻塞 ≤35s)                │                              │
     ├── 收到 msgs                  │                              │
     ├── save_sync_buf(new_buf)     │                              │
     ├── for msg in msgs:           │                              │
     │     queue.push(msg)          │                              │
     │     queue_cv.notify()        │                              │
     │                              ├── queue_cv.wait()            │
     │                              ├── 取出 msg                   │
     │                              ├── current_msg = msg          │
     │                              ├── agent->submit_input(text)  │
     │                              ├── 启动 typing_thread_ ──────►│
     │                              │                              ├── send_typing()
     │                              │                              ├── sleep(3s)
     │                              │                              ├── send_typing()
     │                              │                              ├── ... (直到 output_ready)
     │                              │                              │
     │                              │   [Agent 内部处理]            │
     │                              │   LLM调用/工具执行/MCP        │
     │                              │                              │
     │                              ├── on_output_ready(output) ◄──┤ 停止 typing
     │                              │   (channel 回调)             │
     │                              ├── reply(output)              │
     │                              │   ├── send_text(to, text, context_token)
     │                              │   └── 完成
     │                              └── 取下一条消息
     │
     └── 继续长轮询
```

### 5.3 Agent 输出回调机制

Agent 的 `on_output_ready` 回调在 Agent 工作线程触发。WeChatChannel 需要让处理线程等到这个回调：

```cpp
// WeChatChannel 启动时注册回调
agent->set_on_output_ready([this](const std::string& output) {
    std::lock_guard<std::mutex> lock(current_msg_mutex_);
    current_output_ = output;
    output_ready_ = true;
    output_cv_.notify_one();
});

// 处理线程等待
void handle_message(const InboundMessage& msg) {
    {
        std::lock_guard<std::mutex> lock(current_msg_mutex_);
        current_context_token_ = msg.context_token;
        current_from_user_id_ = msg.from_user_id;
        output_ready_ = false;
        current_output_.clear();
    }

    agent_->submit_input(msg.items[0].text.value());

    // 等待 Agent 输出
    {
        std::unique_lock<std::mutex> lock(current_msg_mutex_);
        output_cv_.wait(lock, [this] { return output_ready_; });
        reply(current_output_);
    }
}
```

**注意**：Agent 的 auto-continue 机制可能让 `submit_input` 触发多轮。但单用户私聊场景，Agent 输出如果不是请求用户输入（`needs_user_input()==false`），会自动继续。需要在回调里判断：只有当输出是最终答案（不再 auto-continue）时才 reply。

**简化处理**：第一版关闭 Agent 的 auto-continue（`should_auto_continue()==false`），让每次 `submit_input` 对应一次 `on_output_ready`。或者：在回调里检查 `needs_user_input()`，只有需要用户输入时才 reply。

**推荐方案**：第一版禁用 auto-continue。Agent 内部如果需要多轮工具调用，在一个 ReactLoop/PELoop 内完成，最后输出即为最终答案。

### 5.4 错误处理与重连

| 错误场景 | 处理策略 |
|----------|---------|
| 长轮询 HTTP 超时（35s 无消息） | 正常，立即发起新长轮询 |
| 长轮询网络错误 | sleep 5s 后重试，无限重试 |
| 长轮询返回 ret!=0 | 记日志，sleep 5s 重试 |
| send_text 失败 | 重试 3 次（间隔 1s/2s/4s），全失败则记日志丢弃 |
| send_typing 失败 | 忽略（非关键） |
| 鉴权失败（401） | 清除 session 文件，触发重新登录 |
| 扫码超时（2 分钟） | 提示重新运行 |
| Agent 处理异常 | reply 错误提示给用户，继续处理下一条 |

### 5.5 持久化

**session.json**（登录后保存）：
```json
{
  "bot_token": "AARzJWAFAAAB...",
  "base_url": "https://ilinkai.weixin.qq.com",
  "saved_at": "2026-06-16T12:00:00"
}
```

**sync_buf.txt**（每次长轮询后保存）：
- 纯文本，内容是 `get_updates_buf` 字符串
- 首次为空字符串，文件不存在时视为空

**重启行为**：
1. 加载 session.json → set_credentials
2. 加载 sync_buf.txt → 作为初始游标
3. 直接进入长轮询，不重新扫码

**session 失效**：服务端返回鉴权错误 → 删除 session.json → 走登录流程。

## 6. 二维码显示方案

iLink 返回 `qrcode_img_content`（base64 PNG），没有可直接扫码的 URL 文本。

**方案**：保存 PNG + 系统默认程序打开 + 控制台提示路径。

```cpp
bool WeChatChannel::save_and_show_qrcode(const std::string& image_base64) {
    // 1. base64 解码为二进制
    auto png_data = agent::util::base64_decode(image_base64);

    // 2. 写入 qrcode.png
    std::ofstream f(config_.qrcode_file, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png_data.data()), png_data.size());

    // 3. 控制台提示
    std::cout << "QR code saved to: " << config_.qrcode_file << "\n";
    std::cout << "Please scan with WeChat app.\n";

    // 4. 自动打开（Windows: ShellExecuteW）
    if (config_.auto_open_qrcode) {
#ifdef _WIN32
        ShellExecuteW(nullptr, L"open",
            string_to_wstring(config_.qrcode_file).c_str(),
            nullptr, nullptr, SW_SHOWNORMAL);
#endif
    }
    return true;
}
```

**为何不直接控制台打印**：iLink 返回的是 PNG 二进制，不是二维码矩阵数据。要在控制台打印需要 PNG 解码 + 二维码图像识别（识别矩阵的黑白格），需要引入 libpng + 二维码识别库，复杂度过高。保存文件 + 系统打开是最实用的方案。

## 7. CLI 集成

### 7.1 启动参数

```
agent_cli.exe --wechat [--mode react|plan_execute|reflection] [--config <path>]
```

- `--wechat`：启用微信渠道模式（不加则走原 CLI 交互模式）
- `--mode`：Agent 模式，默认 react
- `--config`：wechat.json 配置路径，默认 `config/wechat.json`

### 7.2 main.cpp 改动

```cpp
int main(int argc, char* argv[]) {
    // ... 现有参数解析

    bool wechat_mode = args.count("--wechat") > 0;

    if (wechat_mode) {
        return run_wechat_mode(args);
    }

    // ... 原有 CLI 模式
}

int run_wechat_mode(const Args& args) {
    // 1. 加载 wechat.json
    auto wechat_cfg = load_wechat_config(args.config_path);

    // 2. 创建 HTTP 客户端
    auto http = agent::create_http_client();

    // 3. 创建 IlinkClient
    agent_cli::channels::wechat::IlinkConfig ilink_cfg;
    // ... 从 wechat.json 填充
    agent_cli::channels::wechat::IlinkClient ilink(*http, ilink_cfg);

    // 4. 创建 Agent（复用现有 make_agent 逻辑）
    auto agent = make_agent(args.mode, ...);  // 注册工具/MCP/记忆

    // 5. 创建并启动 Channel
    agent_cli::channels::wechat::WeChatChannelConfig ch_cfg;
    // ... 从 wechat.json 填充
    agent_cli::channels::wechat::WeChatChannel channel(ilink, ch_cfg);

    if (!channel.start(*agent)) {
        std::cerr << "Failed to start WeChat channel\n";
        return 1;
    }

    // 6. 等待退出
    std::cout << "WeChat channel running. Press Ctrl+C to stop.\n";
    // 注册信号处理
    std::signal(SIGINT, signal_handler);
    while (!g_should_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    channel.stop();
    agent->stop();
    return 0;
}
```

### 7.3 wechat.json 配置示例

```json
{
  "ilink": {
    "base_url": "https://ilinkai.weixin.qq.com",
    "channel_version": "1.0.2"
  },
  "channel": {
    "session_file": "data/wechat/session.json",
    "sync_buf_file": "data/wechat/sync_buf.txt",
    "qrcode_file": "data/wechat/qrcode.png",
    "auto_open_qrcode": true,
    "enable_typing": true,
    "typing_interval_ms": 3000,
    "reconnect_delay_ms": 5000
  }
}
```

## 8. 依赖与复用

| 能力 | 来源 | 复用方式 |
|------|------|---------|
| HTTP/HTTPS | `WinHttpClient` (已有) | 直接用 `create_http_client()` |
| JSON | `nlohmann/json` (已有) | 直接用 |
| Agent 引擎 | `agent_lib` (已有) | `Agent::create_react()` 等 |
| 工具/MCP/记忆 | agent_cli 现有注册逻辑 | 复用 `make_agent` |
| Base64 | **新增** | `agent_lib/src/util/base64.cpp` |
| 文件写入 | 标准库 | `std::ofstream` |
| 系统打开图片 | Windows API | `ShellExecuteW` |

**无新增第三方依赖**，仅新增一个 base64 工具（纯标准库实现）。

## 9. 实现任务分解

### 阶段 1：基础设施
- [ ] T1: 实现 `base64.h/cpp`（编码 + 解码）
- [ ] T2: 在 `agent_lib/CMakeLists.txt` 注册 base64 源文件
- [ ] T3: 单元测试 base64

### 阶段 2：IlinkClient 协议封装
- [ ] T4: 实现 `ilink_client.h/cpp` 框架（构造、配置、generate_wechat_uin、build_auth_headers）
- [ ] T5: 实现 `get_login_qrcode()` + `poll_qrcode_status()`
- [ ] T6: 实现 `get_updates()`
- [ ] T7: 实现 `send_text()`
- [ ] T8: 实现 `get_typing_ticket()` + `send_typing()`
- [ ] T9: IlinkClient 单元测试（用 MockHttpClient）

### 阶段 3：WeChatChannel 渠道层
- [ ] T10: 实现 `wechat_channel.h/cpp` 框架（成员、持久化方法）
- [ ] T11: 实现 `do_login()` + `save_and_show_qrcode()`
- [ ] T12: 实现 `save_session/load_session/save_sync_buf/load_sync_buf`
- [ ] T13: 实现 `poll_loop()`（长轮询 + 队列推送 + 重连）
- [ ] T14: 实现 `process_loop()` + `handle_message()`（submit_input + 等待 output + reply）
- [ ] T15: 实现 `typing_loop()`
- [ ] T16: 实现 `start()` / `stop()` 生命周期

### 阶段 4：CLI 集成
- [ ] T17: main.cpp 新增 `--wechat` 参数解析
- [ ] T18: 实现 `run_wechat_mode()`（配置加载 + 创建 Agent + 创建 Channel + 等待退出）
- [ ] T19: 创建 `wechat.json.example` 配置模板
- [ ] T20: 更新 `.gitignore`（data/wechat/）

### 阶段 5：编译与测试
- [ ] T21: 更新 `agent_cli/CMakeLists.txt`（新增 channels 目录源文件）
- [ ] T22: 编译通过
- [ ] T23: 手动扫码登录测试
- [ ] T24: 微信发消息 → Agent 回复 端到端测试
- [ ] T25: 重启复用 session 测试
- [ ] T26: 网络断开重连测试

## 10. 测试方案

### 10.1 单元测试（framework_test）

**base64 测试**：
- 编码 "hello" → "aGVsbG8="
- 解码 "aGVsbG8=" → "hello"
- 空字符串
- 二进制数据（含 0x00）
- 长字符串

**IlinkClient 测试**（用 MockHttpClient）：
- `generate_wechat_uin()` 生成格式正确（base64，每次不同）
- `build_auth_headers()` 包含所有必需头
- `get_login_qrcode()` 正确解析响应
- `poll_qrcode_status()` 状态机转换（pending→scanned→confirmed）
- `get_updates()` 解析消息 + 返回新游标
- `send_text()` 请求体格式正确（context_token 带回）
- HTTP 错误返回 nullopt

### 10.2 集成测试（手动，需真实微信账号）

由于 iLink API 需要真实微信扫码，无法自动化，以下为手动测试清单：

| 测试项 | 步骤 | 预期 |
|--------|------|------|
| 首次登录 | `agent_cli --wechat` → 扫码 | 控制台显示二维码，扫码后提示登录成功 |
| 消息收发 | 微信发"你好" | Agent 处理后微信收到回复 |
| 工具调用 | 微信发"读一下 C:\test.txt" | Agent 调用 read_file，回复文件内容 |
| 多轮对话 | 连续发多条消息 | 排队处理，每条都有回复 |
| Typing | 发消息后观察微信 | 处理期间显示"正在输入" |
| Session 复用 | 重启 agent_cli | 不用重新扫码，直接收发消息 |
| 网络断开 | 拔网线 10s 再插回 | 自动重连，继续收消息 |
| 中文消息 | 发中文内容 | 编码正确，回复无乱码 |
| 长消息 | 发很长的输入 | Agent 处理不超时，回复完整 |
| 退出 | Ctrl+C | 优雅停止，session 已保存 |

### 10.3 边界场景

- Agent 处理超时（LLM 无响应）：reply 错误提示
- 用户发空消息：忽略
- 用户发非文本消息（图片/语音）：回复"暂不支持非文本消息"
- 同一消息重复收到（游标未更新）：用 message 去重（基于 context_token 或消息内容 hash）

## 11. 已知限制与风险

| 项 | 说明 | 缓解 |
|----|------|------|
| OpenClaw 账号体系 | 协议文档提到"推测需要 OpenClaw 平台审核"，但裸调 demo 看似可用 | 实测确认，若需要注册则补充注册流程 |
| bot_type=3 含义 | 源码硬编码，未明确 | 沿用，后续观察 |
| 速率限制 | 官方未公开 | 实测，必要时加限流 |
| 腾讯可终止服务 | 条款 7.2 | 不作核心依赖，保留 CLI 模式作为降级 |
| 群聊不可用 | iLink bot 身份群聊投递不稳定 | 第一版不支持，私聊优先 |
| token 有效期 | 未知 | 鉴权失败自动重新登录 |
| 媒体消息 | AES-128-ECB + CDN 复杂 | 第一版只支持文本 |

## 12. 后续扩展（不在第一版）

- 多用户会话池（每用户独立 ContextManager + context_token 映射）
- 媒体消息（图片/语音/文件，需 AES-128-ECB + CDN）
- 群聊支持（待 iLink 群聊能力稳定后）
- 流式回复（message_state 流式状态，边生成边发）
- 主动消息推送（不依赖 inbound 的 context_token，需 getconfig 获取 owner 上下文）
- Webhook 模式（替代长轮询，需公网 IP）

---

**待审核要点**：
1. 串行处理模型（单用户排队）是否可接受？
2. 二维码"保存文件+系统打开"方案是否可接受（而非控制台直接打印）？
3. 第一版禁用 Agent auto-continue 是否可接受？
4. 是否同意新增 base64 工具到 agent_lib？
5. 文件结构和命名是否符合项目规范？
