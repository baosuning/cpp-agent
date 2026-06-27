# WeChat Channel 图片消息收发 设计规格说明

> **Status: 设计中**
>
> 在现有 WeChat Channel（iLink Bot API）文本消息基础上，增加图片消息的接收和发送能力。

## Why

当前 WeChat Channel 仅支持文本消息，用户发送图片时 Agent 无法感知，Agent 也无法向用户发送图片。图片是最常见的非文本消息类型，支持图片收发是 WeChat Channel 实用化的关键一步。

## What Changes

- **InboundItem / InboundMessage** 扩展：新增图片字段（CDN 下载信息），`get_updates` 解析 `type=2` 图片项
- **IlinkClient** 新增：`send_image`（发送图片）、`download_image`（从 CDN 下载并解密图片）、`get_upload_url`（获取 CDN 上传地址）
- **CDN 加解密**：AES-128-ECB 加密/解密工具（无外部依赖，纯标准库实现）
- **WeChatChannel** 改动：`handle_message` 中区分文本/图片消息，图片消息转为文本描述传给 Agent（第一版不修改 Agent 的 Message 结构）
- **Agent 层**：第一版不修改 `Message` 结构体，图片以文本描述形式传递给 LLM

## Impact

- Affected specs: `wechat-channel`
- Affected code: `ilink_client.h/cpp`, `wechat_channel.h/cpp`, 新增 `aes_utils.h/cpp`
- **不破坏**现有文本消息收发逻辑

---

## ADDED Requirements

### Requirement: 图片消息接收

系统 SHALL 在 `get_updates` 响应中解析 `type=2`（图片）的 item，提取 CDN 下载信息，并转换为可供 Agent 处理的文本描述。

#### Scenario: 用户发送单张图片

- **GIVEN** 用户通过微信向 Bot 发送一张图片
- **WHEN** `get_updates` 返回包含 `type=2` item 的消息
- **THEN** `InboundItem` 正确解析 `image_item` 字段（含 `encrypt_query_param`, `aes_key`, `width`, `height`, `md5`）
- **AND** `handle_message` 识别为图片消息，下载并解密图片数据
- **AND** 将图片转为文本描述（如 `[Image: 1920x1080, format: jpeg, md5: xxx]`）提交给 Agent
- **AND** Agent 可以基于此描述进行回复

#### Scenario: 图片 + 文本混合消息

- **GIVEN** 用户发送一条同时包含图片和文本的消息
- **WHEN** `get_updates` 返回 `item_list` 中同时有 `type=1` 和 `type=2`
- **THEN** 文本和图片描述拼接后一起提交给 Agent
- **AND** 拼装格式：`[Image: WxH] 用户的文本内容`

#### Scenario: 多张图片

- **GIVEN** 用户发送多条图片消息（或一次包含多张图片）
- **WHEN** `item_list` 中有多个 `type=2` item
- **THEN** 每张图片都独立描述

---

### Requirement: 图片消息发送

系统 SHALL 支持通过 `sendmessage` 发送图片消息，包括 CDN 上传流程。

#### Scenario: 发送本地图片文件

- **GIVEN** Agent 需要向用户发送一张本地图片
- **WHEN** 调用 `send_image` 传入本地文件路径
- **THEN** 系统读取文件、生成随机 AES-128-ECB 密钥、加密图片数据
- **AND** 调用 `get_upload_url` 获取 CDN 预签名上传地址
- **AND** HTTP PUT 上传加密后的图片数据到 CDN
- **AND** 调用 `sendmessage` 发送 `type=2` item（含 `file_id`, `aes_key`, `md5`, `width`, `height`）
- **AND** 用户在微信中收到图片

#### Scenario: 发送失败重试

- **GIVEN** CDN 上传失败
- **WHEN** `get_upload_url` 网络错误 或 HTTP PUT 返回非 200
- **THEN** `send_image` 返回 false
- **AND** 记录错误日志，不阻塞后续消息处理

---

### Requirement: CDN 图片下载与解密

系统 SHALL 从 CDN 下载加密图片并使用 AES-128-ECB 解密。

#### Scenario: 下载并解密图片

- **GIVEN** 收到图片消息中的 `encrypt_query_param` 和 `aes_key`
- **WHEN** 调用 `download_image`
- **THEN** 构造 CDN 下载 URL（`https://novac2c.cdn.weixin.qq.com/c2c/<encrypt_query_param>`）
- **AND** HTTP GET 下载加密字节
- **AND** 使用 `aes_key` 进行 AES-128-ECB 解密
- **AND** 返回解密后的图片二进制数据

#### Scenario: CDN 下载失败

- **GIVEN** CDN 下载 HTTP 错误
- **WHEN** GET 请求失败或返回非 200
- **THEN** 返回空 `std::vector<uint8_t>`
- **AND** 记录错误日志
- **AND** `handle_message` 仍将图片的基本信息（尺寸、md5）作为文本描述传给 Agent

---

### Requirement: AES-128-ECB 加解密工具

系统 SHALL 提供 AES-128-ECB 加解密工具，无外部依赖，基于标准库实现。

#### Scenario: 加密二进制数据

- **GIVEN** 一段明文二进制数据和 16 字节密钥
- **WHEN** 调用 `aes_128_ecb_encrypt`
- **THEN** 返回 AES-128-ECB 加密后的密文
- **AND** 明文长度不足 16 字节倍数时使用 PKCS7 填充

#### Scenario: 解密二进制数据

- **GIVEN** 一段密文二进制数据和 16 字节密钥
- **WHEN** 调用 `aes_128_ecb_decrypt`
- **THEN** 返回 AES-128-ECB 解密后的明文
- **AND** 自动去除 PKCS7 填充

---

### Requirement: get_upload_url 端点

系统 SHALL 实现 `/ilink/bot/getuploadurl` 端点封装。

#### Scenario: 获取 CDN 上传地址

- **GIVEN** 已登录的 IlinkClient
- **WHEN** 调用 `get_upload_url` 传入文件类型（如 `"image/jpeg"`）
- **THEN** POST `/ilink/bot/getuploadurl` 请求
- **AND** 解析返回的 `upload_url`（预签名 CDN 地址）、`file_id`（CDN 文件标识）
- **AND** 返回 `UploadUrlResult` 结构体

---

## MODIFIED Requirements

### Requirement: InboundItem 结构扩展

**原有**：
```cpp
struct InboundItem {
    int                         type = 0;   // 1=文本
    std::optional<std::string>  text;       // type=1 时填充
};
```

**修改为**：
```cpp
struct InboundItem {
    int                         type = 0;   // 1=文本, 2=图片
    // 文本字段（type=1）
    std::optional<std::string>  text;
    // 图片字段（type=2）
    std::optional<std::string>  image_encrypt_param; // CDN 下载参数
    std::optional<std::string>  image_aes_key;       // AES-128 密钥（字符串或 hex）
    int                         image_width  = 0;
    int                         image_height = 0;
    std::optional<std::string>  image_md5;
    std::optional<std::string>  image_format;        // "jpeg", "png" 等
};
```

### Requirement: IlinkClient 接口扩展

**新增方法**：

```cpp
// 获取 CDN 上传地址
struct UploadUrlResult {
    std::string upload_url;  // 预签名 CDN 地址
    std::string file_id;     // CDN 文件标识
    int         expire_seconds = 0;
};
std::optional<UploadUrlResult> get_upload_url(const std::string& content_type);

// 从 CDN 下载并解密图片
std::vector<uint8_t> download_image(const std::string& encrypt_query_param,
                                    const std::string& aes_key_hex);

// 发送图片消息
bool send_image(const std::string& to_user_id,
                const std::string& file_path,
                const std::string& context_token);
```

### Requirement: WeChatChannel handle_message 改动

**原有**：只处理 `type=1`（文本），非文本直接回复不支持。

**修改为**：遍历 `item_list`，分别处理文本和图片：
- 文本：拼接 `user_text`
- 图片：尝试下载解密 → 生成文本描述 `[Image: WxH, format: xxx]`，拼接到 `user_text`（下载失败时也生成基础描述）
- 如果 `user_text` 最终为空（纯图片且无文本），也提交给 Agent 处理

---

## 数据流设计

### 图片接收流程

```
iLink Server → get_updates 响应
  │
  ├── msg.items[] 包含 type=2 的 item
  │   image_item = { encrypt_query_param, aes_key, width, height, md5, ... }
  │
  ▼
IlinkClient::get_updates 解析 → InboundItem (image_* 字段填充)
  │
  ▼
WeChatChannel::handle_message
  ├── 遍历 items
  │   ├── type=1: 拼接文本
  │   └── type=2: 调用 client_->download_image(encrypt_param, aes_key)
  │       ├── 成功: 获取图片二进制 -> 暂存（后续可扩展为直接传给 Agent 的 vision 能力）
  │       │          生成文本描述: "[Image: WxH, format: xxx]"
  │       └── 失败: 生成降级描述: "[Image: WxH, md5: xxx]"
  │
  ├── user_text = 文本 + 图片描述
  └── agent_->submit_input(user_text)
```

### 图片发送流程

```
WeChatChannel::reply (未来扩展：检测输出中是否包含图片路径)
  │
  ▼
IlinkClient::send_image(file_path, to_user_id, context_token)
  ├── 1. 读取文件二进制
  ├── 2. 生成随机 16 字节 AES key
  ├── 3. aes_128_ecb_encrypt(data, key) → 加密数据
  ├── 4. get_upload_url("image/jpeg") → upload_url, file_id
  ├── 5. HTTP PUT upload_url (加密数据) → CDN
  ├── 6. sendmessage { type: 2, image_item: { file_id, aes_key: hex(key), md5, width, height } }
  └── 7. 返回 true/false
```

---

## 文件结构

```
agent_cli/src/channels/wechat/
├── ilink_client.h          # 修改：新增 InboundItem 图片字段、UploadUrlResult、
│                           #       send_image、download_image、get_upload_url 声明
├── ilink_client.cpp        # 修改：get_updates 解析 type=2、send_image 实现、
│                           #       download_image 实现、get_upload_url 实现
├── wechat_channel.h        # 不变
├── wechat_channel.cpp      # 修改：handle_message 支持图片消息
├── aes_utils.h             # 新增：AES-128-ECB 加解密工具声明
└── aes_utils.cpp           # 新增：AES-128-ECB 加解密工具实现（纯标准库）
```

---

## 依赖与复用

| 能力 | 来源 | 复用方式 |
|------|------|---------|
| HTTP/HTTPS | `WinHttpClient` (已有) | 直接用 `http_.get/post` |
| JSON | `nlohmann/json` (已有) | 直接用 |
| Base64 | `agent_lib/src/util/base64.cpp` (已有) | 直接用 |
| MD5 | 待新增 | 纯标准库实现，用于发送图片时计算文件 MD5 |
| AES-128-ECB | 待新增 | 纯标准库实现，用于 CDN 图片加解密 |
| Agent 引擎 | `agent_lib` (已有) | 不修改，图片以文本描述传入 |

**无新增第三方依赖**。MD5 和 AES-128-ECB 均为纯标准库实现。

---

## 已知限制与后续扩展

| 项 | 说明 | 后续计划 |
|----|------|---------|
| 图片不传给 LLM vision | 第一版只转文本描述，不修改 `Message` 结构支持 `image_url` | 后续扩展 Agent 层支持多模态输入 |
| 图片发送需 Agent 主动调用 | 第一版 `send_image` 实现但不集成到 Agent 自动回复中 | 后续作为工具暴露给 Agent |
| 不支持语音/文件/视频 | 非目标 | 后续按需扩展 |
| 只支持 JPEG/PNG | CDN 上传时指定 content_type | 后续按需扩展 |
| AES-128-ECB 安全性 | ECB 模式不推荐用于生产加密，但这是 iLink CDN 的协议要求 | 不可更改，协议使然 |