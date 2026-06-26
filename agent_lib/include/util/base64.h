#pragma once
// agent_lib/include/util/base64.h
// 标准 Base64 编解码工具（RFC 4648），纯标准库实现，无外部依赖

#include <string>
#include <vector>

namespace agent::util {

// 将字节序列编码为 Base64 字符串
std::string base64_encode(const std::string& data);
std::string base64_encode(const std::vector<unsigned char>& data);

// 将 Base64 字符串解码为字节序列
// 输入非法时返回空 vector
std::vector<unsigned char> base64_decode(const std::string& encoded);

// 将 Base64 字符串解码为字符串（适用于文本场景）
std::string base64_decode_to_string(const std::string& encoded);

} // namespace agent::util
