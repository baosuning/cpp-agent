#pragma once
#include <vector>
#include <cstdint>
#include <string>

namespace agent::util {

// ============================================================================
// AES-128-ECB 加解密（用于 iLink CDN 图片加解密）
// 注意：ECB 模式由 iLink CDN 协议要求，不可更改
// ============================================================================

// AES-128-ECB 加密（含 PKCS7 填充）
// key 必须是 16 字节，否则抛出 std::runtime_error
std::vector<uint8_t> aes_128_ecb_encrypt(const std::vector<uint8_t>& plaintext,
                                         const std::vector<uint8_t>& key);

// AES-128-ECB 解密（自动去除 PKCS7 填充）
// key 必须是 16 字节，ciphertext 长度必须是 16 的倍数，否则抛出 std::runtime_error
std::vector<uint8_t> aes_128_ecb_decrypt(const std::vector<uint8_t>& ciphertext,
                                         const std::vector<uint8_t>& key);

// ============================================================================
// 加密安全的随机数生成
// ============================================================================

// 生成加密安全的随机字节（使用操作系统 CSPRNG）
// 失败时抛出 std::runtime_error
std::vector<uint8_t> generate_random_bytes(size_t count);

// 生成加密安全的 16 字节 AES-128 密钥
std::vector<uint8_t> generate_aes_key();

// ============================================================================
// hex 编解码
// ============================================================================

// hex 字符串 → 字节数组（长度必须为偶数，否则抛出 std::runtime_error）
std::vector<uint8_t> hex_decode(const std::string& hex);

// 字节数组 → 小写 hex 字符串
std::string hex_encode(const std::vector<uint8_t>& data);

// ============================================================================
// MD5 哈希（用于 iLink CDN 文件校验）
// ============================================================================

// 计算二进制数据的 MD5，返回小写 hex 字符串
std::string md5_hex(const std::vector<uint8_t>& data);

// 计算文件的 MD5，返回小写 hex 字符串
// 文件不存在或读取失败时抛出 std::runtime_error
std::string md5_file_hex(const std::string& file_path);

// ============================================================================
// 图片尺寸解析（用于 send_image 填充 width/height）
// ============================================================================

// 从图片二进制数据解析尺寸，支持 JPEG/PNG/GIF/WebP
// 解析失败返回 {0, 0}
struct ImageSize {
    int width  = 0;
    int height = 0;
};
ImageSize parse_image_size(const std::vector<uint8_t>& data);

} // namespace agent::util
