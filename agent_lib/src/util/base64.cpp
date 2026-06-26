// agent_lib/src/util/base64.cpp
// 标准 Base64 编解码实现（RFC 4648）

#include "util/base64.h"

namespace agent::util {

namespace {

constexpr char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 解码表：将字符映射到 0-63，非法字符映射到 -1
struct DecodeTable {
    int8_t v[256];
    DecodeTable() {
        for (int i = 0; i < 256; ++i) v[i] = -1;
        for (int i = 0; i < 64; ++i) {
            v[static_cast<unsigned char>(kEncodeTable[i])] = static_cast<int8_t>(i);
        }
        // '=' 作为 padding，映射为 -2（合法但非数据）
        v[static_cast<unsigned char>('=')] = -2;
    }
};

const DecodeTable& decode_table() {
    static const DecodeTable t;
    return t;
}

} // namespace

std::string base64_encode(const std::vector<unsigned char>& data) {
    const size_t in_len = data.size();
    if (in_len == 0) return {};

    // 输出长度：每 3 字节 → 4 字符，不足补 '='
    const size_t out_len = ((in_len + 2) / 3) * 4;
    std::string out;
    out.reserve(out_len);

    size_t i = 0;
    // 完整的 3 字节组
    while (i + 3 <= in_len) {
        uint32_t triple = (uint32_t(data[i]) << 16) |
                          (uint32_t(data[i + 1]) << 8) |
                          uint32_t(data[i + 2]);
        out.push_back(kEncodeTable[(triple >> 18) & 0x3F]);
        out.push_back(kEncodeTable[(triple >> 12) & 0x3F]);
        out.push_back(kEncodeTable[(triple >> 6) & 0x3F]);
        out.push_back(kEncodeTable[triple & 0x3F]);
        i += 3;
    }
    // 剩余 1 或 2 字节
    const size_t rem = in_len - i;
    if (rem == 1) {
        uint32_t triple = uint32_t(data[i]) << 16;
        out.push_back(kEncodeTable[(triple >> 18) & 0x3F]);
        out.push_back(kEncodeTable[(triple >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t triple = (uint32_t(data[i]) << 16) |
                          (uint32_t(data[i + 1]) << 8);
        out.push_back(kEncodeTable[(triple >> 18) & 0x3F]);
        out.push_back(kEncodeTable[(triple >> 12) & 0x3F]);
        out.push_back(kEncodeTable[(triple >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::string base64_encode(const std::string& data) {
    return base64_encode(std::vector<unsigned char>(data.begin(), data.end()));
}

std::vector<unsigned char> base64_decode(const std::string& encoded) {
    const auto& tbl = decode_table();
    std::vector<unsigned char> out;

    // 收集合法字符，忽略空白
    std::vector<int8_t> values;
    values.reserve(encoded.size());
    for (unsigned char c : encoded) {
        int8_t val = tbl.v[c];
        if (val == -2 || val >= 0) {
            // val >= 0: 合法数据字符；val == -2: '=' padding
            values.push_back(val);
        }
        // 其他（val == -1）为非法/空白字符，忽略
    }

    // Base64 长度必须是 4 的倍数
    if (values.empty()) return {};
    if (values.size() % 4 != 0) return {};

    out.reserve((values.size() / 4) * 3);

    for (size_t i = 0; i < values.size(); i += 4) {
        int8_t a = values[i];
        int8_t b = values[i + 1];
        int8_t c = values[i + 2];
        int8_t d = values[i + 3];

        // 第一个字符不能是 padding
        if (a < 0 || b < 0) return {};

        uint32_t triple = (uint32_t(a) << 18) | (uint32_t(b) << 12);
        out.push_back(static_cast<unsigned char>((triple >> 16) & 0xFF));

        if (c >= 0) {
            triple |= uint32_t(c) << 6;
            out.push_back(static_cast<unsigned char>((triple >> 8) & 0xFF));
            if (d >= 0) {
                triple |= uint32_t(d);
                out.push_back(static_cast<unsigned char>(triple & 0xFF));
            }
        }
    }
    return out;
}

std::string base64_decode_to_string(const std::string& encoded) {
    auto bytes = base64_decode(encoded);
    return std::string(bytes.begin(), bytes.end());
}

} // namespace agent::util
