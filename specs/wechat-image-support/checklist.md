# Checklist: WeChat Channel 图片消息收发

## 基础设施
- [x] `aes_utils.h` 声明 `aes_128_ecb_encrypt`, `aes_128_ecb_decrypt`, `hex_encode`, `hex_decode` 函数
- [x] AES-128-ECB 加密：已知明文 + 密钥 → 密文正确（与标准 AES 实现一致）
- [x] AES-128-ECB 解密：已知密文 + 密钥 → 明文正确
- [x] PKCS7 填充：加密后长度 = ((len/16)+1)*16；解密后去除填充
- [x] MD5：已知文件 → MD5 hex 正确（与 certutil -hashfile 一致）
- [x] hex 编解码：往返（encode → decode）结果一致

## InboundItem 结构扩展
- [x] `InboundItem` 包含 `image_encrypt_param`, `image_aes_key`, `image_width`, `image_height`, `image_md5`, `image_format` 字段
- [x] 现有 `text` 字段不变，不影响文本消息解析

## get_updates 图片解析
- [x] `type=2` item 的 `image_item` 子对象正确解析
- [x] `encrypt_query_param`, `aes_key`, `width`, `height`, `md5`, `format` 字段正确提取
- [x] 非图片消息（`type=1`）解析不受影响

## get_upload_url 端点
- [x] POST `/ilink/bot/getuploadurl` 请求体格式正确
- [x] 返回的 `upload_url`, `file_id`, `expire_seconds` 正确解析
- [x] 网络错误时返回 `std::nullopt`

## download_image
- [x] CDN URL 构造正确（`https://novac2c.cdn.weixin.qq.com/c2c/<param>`）
- [x] HTTP GET 下载加密字节
- [x] hex 解码 aes_key → 16 字节密钥
- [x] AES-128-ECB 解密正确
- [x] 下载失败时返回空 vector，记日志

## send_image
- [x] 读取本地文件二进制
- [x] 生成随机 16 字节 AES key
- [x] 加密文件数据
- [x] 调用 get_upload_url 获取上传地址
- [x] HTTP PUT 上传加密数据到 CDN
- [x] sendmessage body 格式正确（type=2, image_item 含所有必需字段）
- [x] 发送失败时返回 false，记日志

## WinHttpClient PUT 方法
- [x] `IHttpClient` 新增 `virtual HttpResponse put(...) = 0`
- [x] `WinHttpClient` 实现 PUT 方法
- [x] 现有 GET/POST 功能不受影响

## handle_message 图片集成
- [x] 图片消息（type=2）能正常处理，不再拒绝
- [x] 图片下载成功时生成 `[Image: WxH, format: xxx]` 描述
- [x] 图片下载失败时生成降级描述 `[Image: WxH, md5: xxx]`
- [x] 文本 + 图片混合消息正常拼接
- [x] 纯图片消息（无文本）也能提交给 Agent 处理
- [x] 纯文本消息处理逻辑不变

## 编译与测试
- [x] `build.ps1` 编译通过
- [x] `framework_test.exe` 103/103 全部通过
- [x] `tool_test.exe` 23/23 全部通过
- [ ] 手动测试：微信发图片 → Agent 收到描述并回复（需真实微信环境，T13）