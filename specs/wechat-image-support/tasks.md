# Tasks: WeChat Channel 图片消息收发

## 阶段 1：基础设施（AES + MD5）
- [x] T1: 实现 `aes_utils.h` 和 `aes_utils.cpp`（AES-128-ECB 加解密 + PKCS7 填充）
  - [x] T1.1: 实现 AES-128 核心算法（SubBytes, ShiftRows, MixColumns, AddRoundKey, KeyExpansion）
  - [x] T1.2: 实现 ECB 模式加解密，含 PKCS7 填充/去填充
  - [x] T1.3: 实现 hex 编解码工具函数（密钥在 JSON 中以 hex 字符串传递）
- [x] T2: 实现 MD5 工具（纯标准库，用于计算发送图片的文件 MD5）
  - [x] T2.1: 实现 MD5 核心算法
  - [x] T2.2: 提供 `md5_hex(const std::vector<uint8_t>&)` 和 `md5_hex(const std::string& file_path)` 两个接口

## 阶段 2：IlinkClient 协议层扩展
- [x] T3: 扩展 `InboundItem` 结构（`ilink_client.h`），新增图片字段
  - [x] T3.1: 新增 `image_encrypt_param`, `image_aes_key`, `image_width`, `image_height`, `image_md5`, `image_format` 字段
- [x] T4: 修改 `get_updates` 解析逻辑（`ilink_client.cpp`），解析 `type=2` image_item
  - [x] T4.1: 在 `get_updates` 的 item 解析中增加 `type==2` 分支，解析 `image_item` 子对象
  - [x] T4.2: 提取 `encrypt_query_param`, `aes_key`, `width`, `height`, `md5`, `format` 等字段
- [x] T5: 实现 `get_upload_url` 端点（`ilink_client.cpp`）
  - [x] T5.1: POST `/ilink/bot/getuploadurl`，body 含 `content_type` 和 `base_info`
  - [x] T5.2: 解析返回的 `upload_url`, `file_id`, `expire_seconds`
  - [x] T5.3: 在 `ilink_client.h` 声明 `UploadUrlResult` 结构体和 `get_upload_url` 方法
- [x] T6: 实现 `download_image` 方法（`ilink_client.cpp`）
  - [x] T6.1: 构造 CDN URL：`https://novac2c.cdn.weixin.qq.com/c2c/<encrypt_query_param>`
  - [x] T6.2: HTTP GET 下载加密字节
  - [x] T6.3: hex 解码 `aes_key` → 16 字节密钥
  - [x] T6.4: `aes_128_ecb_decrypt` 解密
  - [x] T6.5: 在 `ilink_client.h` 声明 `download_image` 方法
- [x] T7: 实现 `send_image` 方法（`ilink_client.cpp`）
  - [x] T7.1: 读取本地文件二进制
  - [x] T7.2: 生成随机 16 字节 AES key
  - [x] T7.3: 加密文件数据
  - [x] T7.4: 调用 `get_upload_url` 获取上传地址
  - [x] T7.5: HTTP PUT 上传加密数据到 CDN（WinHttpClient 需支持 PUT 方法）
  - [x] T7.6: 构造 sendmessage body（`type=2`, `image_item` 含 `file_id`, `aes_key`, `md5`, `width`, `height`）
  - [x] T7.7: 调用 `http_post` 发送消息
  - [x] T7.8: 在 `ilink_client.h` 声明 `send_image` 方法

## 阶段 3：WeChatChannel 集成
- [x] T8: 修改 `handle_message` 支持图片消息（`wechat_channel.cpp`）
  - [x] T8.1: 遍历 `item_list` 时，对 `type=2` 调用 `download_image` 尝试下载解密
  - [x] T8.2: 成功时生成文本描述 `[Image: WxH, format: xxx]`，拼接到 user_text
  - [x] T8.3: 失败时生成降级描述 `[Image: WxH, md5: xxx]`，拼接到 user_text
  - [x] T8.4: 移除旧的非文本消息拒绝逻辑（"Sorry, only text messages are supported"）
  - [x] T8.5: 纯图片消息（无文本）也能正常提交给 Agent

## 阶段 4：WinHttpClient PUT 方法支持
- [x] T9: 在 `IHttpClient` 接口和 `WinHttpClient` 中新增 `put` 方法
  - [x] T9.1: `i_http_client.h` 新增纯虚方法 `virtual HttpResponse put(...) = 0`
  - [x] T9.2: `winhttp_client.cpp` 实现 PUT 方法（复用 `do_request` 逻辑，method 改为 "PUT"）

## 阶段 5：编译与测试
- [x] T10: CMakeLists.txt 使用 GLOB_RECURSE，新 `.cpp` 文件自动拾取，无需修改
- [x] T11: 编译通过（`.\build.ps1`）
- [x] T12: `framework_test.exe` 103/103 通过，`tool_test.exe` 23/23 通过
- [ ] T13: 手动测试：微信发送图片 → Agent 收到并回复（需真实微信环境）

# Task Dependencies

- [T2] 无依赖，可与 T1 并行
- [T3] 无依赖，可与 T1/T2 并行
- [T4] 依赖 [T3]
- [T5] 无依赖，可与 T3/T4 并行
- [T6] 依赖 [T1]（AES 解密）
- [T7] 依赖 [T1], [T2], [T5], [T9]（AES 加密 + MD5 + get_upload_url + HTTP PUT）
- [T8] 依赖 [T3], [T4], [T6]
- [T9] 无依赖，可与 T1-T7 并行
- [T10] 依赖 [T1]
- [T11] 依赖 [T1-T10]
- [T12] 依赖 [T11]
- [T13] 依赖 [T12]