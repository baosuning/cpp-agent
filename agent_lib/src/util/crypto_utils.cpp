// crypto_utils.cpp - 加密工具集（AES-128-ECB、MD5、安全随机数、图片尺寸解析）
// 用于 iLink CDN 图片加解密和校验
// 纯 C++ 标准库实现 + Windows CryptAPI（仅随机数生成），无其他外部依赖

#include <util/crypto_utils.h>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")
#endif

namespace agent::util {

// ============================================================================
// AES S-box (standard)
// ============================================================================
static constexpr uint8_t sbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

// ============================================================================
// Inverse AES S-box (standard)
// ============================================================================
static constexpr uint8_t inv_sbox[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d};

// ============================================================================
// Round constants for key expansion
// ============================================================================
static constexpr uint8_t rcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36};

// ============================================================================
// Galois Field multiplication by 2 in GF(2^8)
// ============================================================================
static uint8_t gmul2(uint8_t v) {
    uint8_t r = (uint8_t)(v << 1);
    if (v & 0x80) {
        r ^= 0x1b;
    }
    return r;
}

// ============================================================================
// Galois Field multiplication by 3 in GF(2^8)
// ============================================================================
static uint8_t gmul3(uint8_t v) {
    return gmul2(v) ^ v;
}

// ============================================================================
// Galois Field multiplication in GF(2^8), used for inverse MixColumns
// ============================================================================
static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) {
            p ^= a;
        }
        uint8_t hi = (uint8_t)(a & 0x80);
        a = (uint8_t)(a << 1);
        if (hi) {
            a ^= 0x1b;
        }
        b >>= 1;
    }
    return p;
}

// ============================================================================
// SubWord: apply S-box to each byte of a 4-byte word
// ============================================================================
static void sub_word(uint8_t* word) {
    for (int i = 0; i < 4; i++) {
        word[i] = sbox[word[i]];
    }
}

// ============================================================================
// RotWord: rotate a 4-byte word left by 1 byte
// ============================================================================
static void rot_word(uint8_t* word) {
    uint8_t tmp = word[0];
    word[0] = word[1];
    word[1] = word[2];
    word[2] = word[3];
    word[3] = tmp;
}

// ============================================================================
// Key expansion: generate 11 round keys (176 bytes) from 16-byte key
// ============================================================================
static void key_expansion(const uint8_t* key, uint8_t* round_keys) {
    // First round key is the original key
    for (int i = 0; i < 16; i++) {
        round_keys[i] = key[i];
    }

    uint8_t temp[4];
    for (int i = 4; i < 44; i++) {
        for (int j = 0; j < 4; j++) {
            temp[j] = round_keys[(i - 1) * 4 + j];
        }

        if (i % 4 == 0) {
            rot_word(temp);
            sub_word(temp);
            temp[0] ^= rcon[i / 4];
        }

        for (int j = 0; j < 4; j++) {
            round_keys[i * 4 + j] = round_keys[(i - 4) * 4 + j] ^ temp[j];
        }
    }
}

// ============================================================================
// SubBytes: substitute each byte using S-box
// ============================================================================
static void sub_bytes(uint8_t* state) {
    for (int i = 0; i < 16; i++) {
        state[i] = sbox[state[i]];
    }
}

// ============================================================================
// InvSubBytes: substitute each byte using inverse S-box
// ============================================================================
static void inv_sub_bytes(uint8_t* state) {
    for (int i = 0; i < 16; i++) {
        state[i] = inv_sbox[state[i]];
    }
}

// ============================================================================
// ShiftRows: rotate rows left by row index
// ============================================================================
static void shift_rows(uint8_t* state) {
    // Row 0: no shift
    // Row 1: shift left by 1
    uint8_t tmp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = tmp;

    // Row 2: shift left by 2
    tmp = state[2];
    state[2] = state[10];
    state[10] = tmp;
    tmp = state[6];
    state[6] = state[14];
    state[14] = tmp;

    // Row 3: shift left by 3 (same as right by 1)
    tmp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = tmp;
}

// ============================================================================
// InvShiftRows: rotate rows right by row index
// ============================================================================
static void inv_shift_rows(uint8_t* state) {
    // Row 0: no shift
    // Row 1: shift right by 1
    uint8_t tmp = state[13];
    state[13] = state[9];
    state[9] = state[5];
    state[5] = state[1];
    state[1] = tmp;

    // Row 2: shift right by 2
    tmp = state[10];
    state[10] = state[2];
    state[2] = tmp;
    tmp = state[14];
    state[14] = state[6];
    state[6] = tmp;

    // Row 3: shift right by 3 (same as left by 1)
    tmp = state[3];
    state[3] = state[7];
    state[7] = state[11];
    state[11] = state[15];
    state[15] = tmp;
}

// ============================================================================
// MixColumns: matrix multiplication in GF(2^8)
// state is column-major: [c0r0, c0r1, c0r2, c0r3, c1r0, ...]
// Each column: [s0, s1, s2, s3]
// New column = M * column where M = [2 3 1 1; 1 2 3 1; 1 1 2 3; 3 1 1 2]
// ============================================================================
static void mix_columns(uint8_t* state) {
    for (int c = 0; c < 4; c++) {
        int idx = c * 4;
        uint8_t s0 = state[idx];
        uint8_t s1 = state[idx + 1];
        uint8_t s2 = state[idx + 2];
        uint8_t s3 = state[idx + 3];

        state[idx]     = gmul2(s0) ^ gmul3(s1) ^ s2 ^ s3;
        state[idx + 1] = s0 ^ gmul2(s1) ^ gmul3(s2) ^ s3;
        state[idx + 2] = s0 ^ s1 ^ gmul2(s2) ^ gmul3(s3);
        state[idx + 3] = gmul3(s0) ^ s1 ^ s2 ^ gmul2(s3);
    }
}

// ============================================================================
// InvMixColumns: inverse matrix multiplication in GF(2^8)
// Inverse matrix = [14 11 13 9; 9 14 11 13; 13 9 14 11; 11 13 9 14]
// ============================================================================
static void inv_mix_columns(uint8_t* state) {
    for (int c = 0; c < 4; c++) {
        int idx = c * 4;
        uint8_t s0 = state[idx];
        uint8_t s1 = state[idx + 1];
        uint8_t s2 = state[idx + 2];
        uint8_t s3 = state[idx + 3];

        state[idx]     = gmul(0x0e, s0) ^ gmul(0x0b, s1) ^ gmul(0x0d, s2) ^ gmul(0x09, s3);
        state[idx + 1] = gmul(0x09, s0) ^ gmul(0x0e, s1) ^ gmul(0x0b, s2) ^ gmul(0x0d, s3);
        state[idx + 2] = gmul(0x0d, s0) ^ gmul(0x09, s1) ^ gmul(0x0e, s2) ^ gmul(0x0b, s3);
        state[idx + 3] = gmul(0x0b, s0) ^ gmul(0x0d, s1) ^ gmul(0x09, s2) ^ gmul(0x0e, s3);
    }
}

// ============================================================================
// AddRoundKey: XOR state with round key
// ============================================================================
static void add_round_key(uint8_t* state, const uint8_t* round_key) {
    for (int i = 0; i < 16; i++) {
        state[i] ^= round_key[i];
    }
}

// ============================================================================
// Encrypt a single 16-byte block
// ============================================================================
static void aes_encrypt_block(const uint8_t* plaintext, const uint8_t* round_keys, uint8_t* ciphertext) {
    uint8_t state[16];
    for (int i = 0; i < 16; i++) {
        state[i] = plaintext[i];
    }

    // Initial round: AddRoundKey
    add_round_key(state, round_keys);

    // 9 rounds: SubBytes, ShiftRows, MixColumns, AddRoundKey
    for (int round = 1; round <= 9; round++) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, round_keys + round * 16);
    }

    // Final round (no MixColumns): SubBytes, ShiftRows, AddRoundKey
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, round_keys + 10 * 16);

    for (int i = 0; i < 16; i++) {
        ciphertext[i] = state[i];
    }
}

// ============================================================================
// Decrypt a single 16-byte block
// ============================================================================
static void aes_decrypt_block(const uint8_t* ciphertext, const uint8_t* round_keys, uint8_t* plaintext) {
    uint8_t state[16];
    for (int i = 0; i < 16; i++) {
        state[i] = ciphertext[i];
    }

    // Initial round: AddRoundKey (last round key)
    add_round_key(state, round_keys + 10 * 16);

    // 9 rounds: InvShiftRows, InvSubBytes, AddRoundKey, InvMixColumns
    for (int round = 9; round >= 1; round--) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, round_keys + round * 16);
        inv_mix_columns(state);
    }

    // Final round: InvShiftRows, InvSubBytes, AddRoundKey
    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, round_keys);

    for (int i = 0; i < 16; i++) {
        plaintext[i] = state[i];
    }
}

// ============================================================================
// PKCS7 padding: add N bytes of value N
// ============================================================================
static std::vector<uint8_t> pkcs7_pad(const std::vector<uint8_t>& data) {
    size_t pad_len = 16 - (data.size() % 16);
    std::vector<uint8_t> padded = data;
    padded.insert(padded.end(), pad_len, (uint8_t)pad_len);
    return padded;
}

// ============================================================================
// Remove PKCS7 padding: read last byte, verify and remove
// ============================================================================
static std::vector<uint8_t> pkcs7_unpad(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        throw std::runtime_error("PKCS7 unpadding error: empty data");
    }

    uint8_t pad_len = data.back();
    if (pad_len == 0 || pad_len > 16) {
        throw std::runtime_error("PKCS7 unpadding error: invalid padding length " + std::to_string(pad_len));
    }

    // Verify all padding bytes
    for (size_t i = data.size() - pad_len; i < data.size(); i++) {
        if (data[i] != pad_len) {
            throw std::runtime_error("PKCS7 unpadding error: invalid padding bytes");
        }
    }

    return std::vector<uint8_t>(data.begin(), data.end() - pad_len);
}

// ============================================================================
// AES Public API
// ============================================================================

std::vector<uint8_t> aes_128_ecb_encrypt(const std::vector<uint8_t>& plaintext,
                                          const std::vector<uint8_t>& key) {
    if (key.size() != 16) {
        throw std::runtime_error("AES-128 key must be 16 bytes, got " + std::to_string(key.size()));
    }

    // Generate round keys
    uint8_t round_keys[176]; // 11 * 16
    key_expansion(key.data(), round_keys);

    // Apply PKCS7 padding
    std::vector<uint8_t> padded = pkcs7_pad(plaintext);

    // Encrypt each block
    std::vector<uint8_t> ciphertext(padded.size());
    for (size_t i = 0; i < padded.size(); i += 16) {
        aes_encrypt_block(padded.data() + i, round_keys, ciphertext.data() + i);
    }

    return ciphertext;
}

std::vector<uint8_t> aes_128_ecb_decrypt(const std::vector<uint8_t>& ciphertext,
                                          const std::vector<uint8_t>& key) {
    if (key.size() != 16) {
        throw std::runtime_error("AES-128 key must be 16 bytes, got " + std::to_string(key.size()));
    }

    if (ciphertext.size() % 16 != 0) {
        throw std::runtime_error("AES-128 ciphertext length must be multiple of 16, got " + std::to_string(ciphertext.size()));
    }

    // Generate round keys
    uint8_t round_keys[176]; // 11 * 16
    key_expansion(key.data(), round_keys);

    // Decrypt each block
    std::vector<uint8_t> plaintext(ciphertext.size());
    for (size_t i = 0; i < ciphertext.size(); i += 16) {
        aes_decrypt_block(ciphertext.data() + i, round_keys, plaintext.data() + i);
    }

    // Remove PKCS7 padding
    return pkcs7_unpad(plaintext);
}

std::vector<uint8_t> hex_decode(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        throw std::runtime_error("hex_decode: hex string length must be even");
    }

    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);

    for (size_t i = 0; i < hex.size(); i += 2) {
        auto hex_to_nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
            if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
            throw std::runtime_error("hex_decode: invalid hex character");
        };
        uint8_t hi = hex_to_nibble(hex[i]);
        uint8_t lo = hex_to_nibble(hex[i + 1]);
        result.push_back((uint8_t)((hi << 4) | lo));
    }

    return result;
}

std::string hex_encode(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : data) {
        oss << std::setw(2) << (int)byte;
    }
    return oss.str();
}

// ============================================================================
// 加密安全的随机数生成
// ============================================================================

std::vector<uint8_t> generate_random_bytes(size_t count) {
    std::vector<uint8_t> bytes(count);
    if (count == 0) return bytes;

#ifdef _WIN32
    // 使用 Windows CryptAPI (CryptGenRandom) 生成加密安全随机数
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        throw std::runtime_error("generate_random_bytes: CryptAcquireContext failed: " +
                                 std::to_string(GetLastError()));
    }
    BOOL ok = CryptGenRandom(hProv, static_cast<DWORD>(count), bytes.data());
    DWORD err = GetLastError();
    CryptReleaseContext(hProv, 0);
    if (!ok) {
        throw std::runtime_error("generate_random_bytes: CryptGenRandom failed: " + std::to_string(err));
    }
#else
    // 非 Windows 平台使用 /dev/urandom
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count))) {
        throw std::runtime_error("generate_random_bytes: failed to read /dev/urandom");
    }
#endif
    return bytes;
}

std::vector<uint8_t> generate_aes_key() {
    return generate_random_bytes(16);
}

// ============================================================================
// MD5 round functions
// ============================================================================
static inline uint32_t F(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) | (~x & z);
}

static inline uint32_t G(uint32_t x, uint32_t y, uint32_t z) {
    return (x & z) | (y & ~z);
}

static inline uint32_t H(uint32_t x, uint32_t y, uint32_t z) {
    return x ^ y ^ z;
}

static inline uint32_t I(uint32_t x, uint32_t y, uint32_t z) {
    return y ^ (x | ~z);
}

// ============================================================================
// Left rotate
// ============================================================================
static inline uint32_t left_rotate(uint32_t x, uint32_t n) {
    return (x << n) | (x >> (32 - n));
}

// ============================================================================
// MD5 per-round shift amounts
// ============================================================================
static constexpr uint32_t S[64] = {
    7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
    5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
    4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
    6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21};

// ============================================================================
// MD5 K constants: floor(2^32 * abs(sin(i + 1))) for i = 0..63
// ============================================================================
static constexpr uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

// ============================================================================
// MD5 block processing (64-byte blocks)
// ============================================================================
static void md5_transform(const uint8_t* block, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    // Decode 64 bytes into 16 32-bit little-endian words
    uint32_t M[16];
    for (int i = 0; i < 16; i++) {
        M[i] = (uint32_t)block[i * 4]
             | ((uint32_t)block[i * 4 + 1] << 8)
             | ((uint32_t)block[i * 4 + 2] << 16)
             | ((uint32_t)block[i * 4 + 3] << 24);
    }

    uint32_t AA = a, BB = b, CC = c, DD = d;

    // Round 1: F function
    for (int i = 0; i < 16; i++) {
        uint32_t f = F(BB, CC, DD);
        uint32_t g = i;
        uint32_t temp = DD;
        DD = CC;
        CC = BB;
        BB = BB + left_rotate(AA + f + K[i] + M[g], S[i]);
        AA = temp;
    }

    // Round 2: G function
    for (int i = 16; i < 32; i++) {
        uint32_t g_val = G(BB, CC, DD);
        uint32_t g = (5 * i + 1) % 16;
        uint32_t temp = DD;
        DD = CC;
        CC = BB;
        BB = BB + left_rotate(AA + g_val + K[i] + M[g], S[i]);
        AA = temp;
    }

    // Round 3: H function
    for (int i = 32; i < 48; i++) {
        uint32_t h_val = H(BB, CC, DD);
        uint32_t g = (3 * i + 5) % 16;
        uint32_t temp = DD;
        DD = CC;
        CC = BB;
        BB = BB + left_rotate(AA + h_val + K[i] + M[g], S[i]);
        AA = temp;
    }

    // Round 4: I function
    for (int i = 48; i < 64; i++) {
        uint32_t i_val = I(BB, CC, DD);
        uint32_t g = (7 * i) % 16;
        uint32_t temp = DD;
        DD = CC;
        CC = BB;
        BB = BB + left_rotate(AA + i_val + K[i] + M[g], S[i]);
        AA = temp;
    }

    a += AA;
    b += BB;
    c += CC;
    d += DD;
}

// ============================================================================
// Core MD5 computation
// ============================================================================
static std::string md5_compute(const uint8_t* data, size_t len) {
    // Initialize MD5 state
    uint32_t a = 0x67452301;
    uint32_t b = 0xefcdab89;
    uint32_t c = 0x98badcfe;
    uint32_t d = 0x10325476;

    // Process full 64-byte blocks
    size_t offset = 0;
    while (offset + 64 <= len) {
        md5_transform(data + offset, a, b, c, d);
        offset += 64;
    }

    // Padding
    uint8_t padding[128];
    size_t pad_len = 64 - (len % 64);
    if (pad_len < 9) {
        pad_len += 64;
    }

    // Copy remaining bytes
    size_t remaining = len - offset;
    for (size_t i = 0; i < remaining; i++) {
        padding[i] = data[offset + i];
    }

    // Append 0x80
    padding[remaining] = 0x80;

    // Fill remaining with zeros (before length)
    for (size_t i = remaining + 1; i < pad_len - 8; i++) {
        padding[i] = 0;
    }

    // Append original message length in bits (little-endian 64-bit)
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) {
        padding[pad_len - 8 + i] = (uint8_t)(bits >> (i * 8));
    }

    // Process padded blocks
    for (size_t i = 0; i < pad_len; i += 64) {
        md5_transform(padding + i, a, b, c, d);
    }

    // Output as little-endian hex string
    auto to_hex = [](uint32_t val) -> std::string {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (int i = 0; i < 4; i++) {
            oss << std::setw(2) << (int)((uint8_t)(val >> (i * 8)));
        }
        return oss.str();
    };

    return to_hex(a) + to_hex(b) + to_hex(c) + to_hex(d);
}

// ============================================================================
// MD5 Public API
// ============================================================================

std::string md5_hex(const std::vector<uint8_t>& data) {
    return md5_compute(data.data(), data.size());
}

std::string md5_file_hex(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("md5_file_hex: cannot open file: " + file_path);
    }

    // Read entire file into memory
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("md5_file_hex: cannot determine file size: " + file_path);
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer((size_t)size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("md5_file_hex: failed to read file: " + file_path);
    }

    return md5_hex(buffer);
}

// ============================================================================
// 图片尺寸解析（JPEG / PNG / GIF / WebP）
// ============================================================================

ImageSize parse_image_size(const std::vector<uint8_t>& data) {
    ImageSize size;
    if (data.size() < 12) return size;

    // PNG: 89 50 4E 47 0D 0A 1A 0A, IHDR at offset 16 (width=16-19, height=20-23, big-endian)
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
        if (data.size() >= 24) {
            size.width  = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
            size.height = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
        }
        return size;
    }

    // GIF: 47 49 46 38, width=6-7, height=8-9 (little-endian)
    if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x38) {
        size.width  = data[6] | (data[7] << 8);
        size.height = data[8] | (data[9] << 8);
        return size;
    }

    // JPEG: FF D8, scan for SOF0 (FF C0) marker
    if (data[0] == 0xFF && data[1] == 0xD8) {
        size_t i = 2;
        while (i + 9 < data.size()) {
            if (data[i] != 0xFF) { i++; continue; }
            uint8_t marker = data[i + 1];
            // SOF0=0xC0, SOF1=0xC1, SOF2=0xC2
            if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
                if (i + 9 < data.size()) {
                    size.height = (data[i + 5] << 8) | data[i + 6];
                    size.width  = (data[i + 7] << 8) | data[i + 8];
                    return size;
                }
            }
            // Skip to next marker: length is big-endian 2 bytes at i+2
            uint16_t seg_len = (data[i + 2] << 8) | data[i + 3];
            i += 2 + seg_len;
        }
        return size;
    }

    // WebP: RIFF....WEBP
    if (data.size() >= 30 &&
        data[0] == 0x52 && data[1] == 0x49 && data[2] == 0x46 && data[3] == 0x46 &&
        data[8] == 0x57 && data[9] == 0x45 && data[10] == 0x42 && data[11] == 0x50) {
        std::string fourcc(reinterpret_cast<const char*>(&data[12]), 4);
        if (fourcc == "VP8 " && data.size() >= 30) {
            size.width  = (data[26] | (data[27] << 8)) & 0x3FFF;
            size.height = (data[28] | (data[29] << 8)) & 0x3FFF;
        } else if (fourcc == "VP8L" && data.size() >= 25) {
            uint32_t v = data[21] | (data[22] << 8) | (data[23] << 16) | (data[24] << 24);
            size.width  = (v & 0x3FFF) + 1;
            size.height = ((v >> 14) & 0x3FFF) + 1;
        } else if (fourcc == "VP8X" && data.size() >= 30) {
            size.width  = (data[24] | (data[25] << 8) | (data[26] << 16)) + 1;
            size.height = (data[27] | (data[28] << 8) | (data[29] << 16)) + 1;
        }
        return size;
    }

    return size;
}

} // namespace agent::util
