#pragma once
#include <agent/types.h>

namespace agent::llm {

inline u8str sanitize_utf8(const u8str& input) {
    u8str result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (c <= 0x7F) {
            result += static_cast<char8_t>(c);
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 < input.size()) {
                unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
                if (c2 >= 0x80 && c2 <= 0xBF) {
                    result += static_cast<char8_t>(c);
                    result += static_cast<char8_t>(c2);
                    ++i;
                }
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 < input.size()) {
                unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
                unsigned char c3 = static_cast<unsigned char>(input[i + 2]);
                if (c2 >= 0x80 && c2 <= 0xBF && c3 >= 0x80 && c3 <= 0xBF) {
                    if (!(c == 0xE0 && c2 < 0xA0) &&
                        !(c == 0xED && c2 >= 0xA0)) {
                        result += static_cast<char8_t>(c);
                        result += static_cast<char8_t>(c2);
                        result += static_cast<char8_t>(c3);
                        i += 2;
                    }
                }
            }
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 < input.size()) {
                unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
                unsigned char c3 = static_cast<unsigned char>(input[i + 2]);
                unsigned char c4 = static_cast<unsigned char>(input[i + 3]);
                if (c2 >= 0x80 && c2 <= 0xBF && c3 >= 0x80 && c3 <= 0xBF && c4 >= 0x80 && c4 <= 0xBF) {
                    if (!(c == 0xF0 && c2 < 0x90) &&
                        !(c == 0xF4 && c2 >= 0x90)) {
                        result += static_cast<char8_t>(c);
                        result += static_cast<char8_t>(c2);
                        result += static_cast<char8_t>(c3);
                        result += static_cast<char8_t>(c4);
                        i += 3;
                    }
                }
            }
        }
    }

    return result;
}

} // namespace agent::llm

namespace agent::llm {

// std::string 版本，用于 HTTP 响应体等场景
inline std::string sanitize_utf8_string(const std::string& input) {
    std::string result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (c <= 0x7F) {
            result += static_cast<char>(c);
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (i + 1 < input.size()) {
                unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
                if (c2 >= 0x80 && c2 <= 0xBF) {
                    result += static_cast<char>(c);
                    result += static_cast<char>(c2);
                    ++i;
                }
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (i + 2 < input.size()) {
                unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
                unsigned char c3 = static_cast<unsigned char>(input[i + 2]);
                if (c2 >= 0x80 && c2 <= 0xBF && c3 >= 0x80 && c3 <= 0xBF) {
                    if (!(c == 0xE0 && c2 < 0xA0) &&
                        !(c == 0xED && c2 >= 0xA0)) {
                        result += static_cast<char>(c);
                        result += static_cast<char>(c2);
                        result += static_cast<char>(c3);
                        i += 2;
                    }
                }
            }
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (i + 3 < input.size()) {
                unsigned char c2 = static_cast<unsigned char>(input[i + 1]);
                unsigned char c3 = static_cast<unsigned char>(input[i + 2]);
                unsigned char c4 = static_cast<unsigned char>(input[i + 3]);
                if (c2 >= 0x80 && c2 <= 0xBF && c3 >= 0x80 && c3 <= 0xBF && c4 >= 0x80 && c4 <= 0xBF) {
                    if (!(c == 0xF0 && c2 < 0x90) &&
                        !(c == 0xF4 && c2 >= 0x90)) {
                        result += static_cast<char>(c);
                        result += static_cast<char>(c2);
                        result += static_cast<char>(c3);
                        result += static_cast<char>(c4);
                        i += 3;
                    }
                }
            }
        }
    }

    return result;
}

} // namespace agent::llm