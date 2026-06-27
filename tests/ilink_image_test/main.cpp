// ilink_image_test/main.cpp
// IlinkClient 图片下载和发送的异常处理单元测试
// 通过 MockHttpClient 模拟各种错误场景，验证异常处理逻辑的正确性

#include <util/i_http_client.h>
#include <util/crypto_utils.h>
#include <util/log.h>

#include "channels/wechat/ilink_client.h"

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace agent_cli::channels::wechat;

// ============================================================================
// 测试辅助宏
// ============================================================================
int test_count = 0;
int pass_count = 0;

void test_header(const std::string& name) {
    std::cout << "\n========================================\n";
    std::cout << "  Test: " << name << "\n";
    std::cout << "========================================\n";
}

void test_pass(const std::string& msg) {
    ++test_count; ++pass_count;
    std::cout << "  [PASS] " << msg << "\n";
}

void test_fail(const std::string& msg) {
    ++test_count;
    std::cout << "  [FAIL] " << msg << "\n";
}

#define ASSERT_TRUE(cond, msg) \
    do { if (cond) test_pass(msg); else test_fail(msg); } while(0)

#define ASSERT_FALSE(cond, msg) ASSERT_TRUE(!(cond), msg)

#define ASSERT_EQ(actual, expected) \
    ASSERT_TRUE((actual) == (expected), "期望 " #expected "，实际 " #actual)

// ============================================================================
// MockHttpClient - 可配置的 HTTP 客户端模拟器
// ============================================================================
class MockHttpClient : public agent::IHttpClient {
public:
    // 请求记录
    struct Request {
        std::string method;
        std::string url;
        std::string body;
        std::map<std::string, std::string> headers;
    };

    // 响应配置：按 URL 子串匹配
    struct ResponseConfig {
        std::string url_substring;     // URL 中包含的子串
        agent::HttpResponse response;  // 返回的响应
        int call_count = 0;            // 被调用次数
    };

    void set_debug(bool enable) override { debug_ = enable; }

    agent::HttpResponse post(const std::string& url, const std::string& body,
                              const std::map<std::string, std::string>& headers,
                              int timeout_ms) override {
        requests_.push_back({"POST", url, body, headers});
        return find_response("POST", url);
    }

    agent::HttpResponse get(const std::string& url,
                             const std::map<std::string, std::string>& headers,
                             int timeout_ms) override {
        requests_.push_back({"GET", url, "", headers});
        return find_response("GET", url);
    }

    agent::HttpResponse put(const std::string& url, const std::string& body,
                             const std::map<std::string, std::string>& headers,
                             int timeout_ms) override {
        requests_.push_back({"PUT", url, body, headers});
        return find_response("PUT", url);
    }

    // 配置响应：按 URL 子串匹配
    void add_response(const std::string& method, const std::string& url_substring,
                      const agent::HttpResponse& resp) {
        configs_.push_back({method + ":" + url_substring, resp, 0});
    }

    // 获取某个 URL 子串的调用次数
    int get_call_count(const std::string& method, const std::string& url_substring) const {
        std::string key = method + ":" + url_substring;
        for (const auto& c : configs_) {
            if (c.url_substring == key) return c.call_count;
        }
        return 0;
    }

    // 获取所有请求记录
    const std::vector<Request>& get_requests() const { return requests_; }

    // 清空所有配置和记录
    void reset() {
        configs_.clear();
        requests_.clear();
    }

private:
    agent::HttpResponse find_response(const std::string& method, const std::string& url) {
        std::string key = method + ":";
        for (auto& c : configs_) {
            // 配置格式为 "METHOD:url_substring"
            auto colon_pos = c.url_substring.find(':');
            if (colon_pos == std::string::npos) continue;

            std::string cfg_method = c.url_substring.substr(0, colon_pos);
            std::string cfg_url = c.url_substring.substr(colon_pos + 1);

            if (cfg_method == method && url.find(cfg_url) != std::string::npos) {
                c.call_count++;
                return c.response;
            }
        }
        // 默认返回 404
        agent::HttpResponse resp;
        resp.status_code = 404;
        resp.body = R"({"ret":-1,"msg":"not mocked"})";
        return resp;
    }

    std::vector<ResponseConfig> configs_;
    std::vector<Request> requests_;
    bool debug_ = false;
};

// ============================================================================
// 辅助函数：创建临时图片文件
// ============================================================================
std::string create_temp_image_file(const std::string& content, const std::string& ext = ".png") {
    char temp_dir[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_dir);
    std::string path = std::string(temp_dir) + "ilink_test_" + std::to_string(GetTickCount()) + ext;

    std::ofstream file(path, std::ios::binary);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    file.close();
    return path;
}

void delete_file(const std::string& path) {
    std::remove(path.c_str());
}

// ============================================================================
// 辅助函数：创建已认证的 IlinkClient
// ============================================================================
std::unique_ptr<IlinkClient> create_authenticated_client(MockHttpClient& mock) {
    auto client = std::make_unique<IlinkClient>(mock);
    client->set_credentials("fake_bot_token_12345", "https://ilinkai.weixin.qq.com",
                            "fake_user_id", "fake_bot_id");
    return client;
}

// ============================================================================
// 测试用例：download_image 异常处理
// ============================================================================

void test_download_image_cdn_failure() {
    test_header("download_image: CDN 返回非 200");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 配置 CDN 返回 500 错误
    agent::HttpResponse cdn_resp;
    cdn_resp.status_code = 500;
    cdn_resp.body = "Internal Server Error";
    mock.add_response("GET", "novac2c.cdn.weixin.qq.com", cdn_resp);

    auto result = client->download_image("test_param", "00112233445566778899aabbccddeeff");

    ASSERT_TRUE(result.empty(), "CDN 500 错误时返回空 vector");
    ASSERT_FALSE(!result.empty(), "不应返回非空数据");
}

void test_download_image_empty_body() {
    test_header("download_image: CDN 返回空 body");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 配置 CDN 返回 200 但 body 为空
    agent::HttpResponse cdn_resp;
    cdn_resp.status_code = 200;
    cdn_resp.body = "";
    mock.add_response("GET", "novac2c.cdn.weixin.qq.com", cdn_resp);

    auto result = client->download_image("test_param", "00112233445566778899aabbccddeeff");

    // 空 body 长度为 0，是 16 的倍数，AES 解密后 PKCS7 去填充会失败
    ASSERT_TRUE(result.empty(), "空 body 时返回空 vector（PKCS7 去填充失败）");
}

void test_download_image_oversized_body() {
    test_header("download_image: body 超过 20MB 限制");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 配置 CDN 返回 200 但 body 超过 20MB
    agent::HttpResponse cdn_resp;
    cdn_resp.status_code = 200;
    cdn_resp.body = std::string(kMaxImageDownloadSize + 1, 'x');
    mock.add_response("GET", "novac2c.cdn.weixin.qq.com", cdn_resp);

    auto result = client->download_image("test_param", "00112233445566778899aabbccddeeff");

    ASSERT_TRUE(result.empty(), "超过 20MB 限制时返回空 vector");
    ASSERT_EQ(mock.get_call_count("GET", "novac2c.cdn.weixin.qq.com"), 1);
}

void test_download_image_invalid_aes_key_odd_length() {
    test_header("download_image: aes_key hex 长度为奇数");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 配置 CDN 返回有效的加密数据
    agent::HttpResponse cdn_resp;
    cdn_resp.status_code = 200;
    cdn_resp.body = std::string(32, 'x');  // 32 字节假数据
    mock.add_response("GET", "novac2c.cdn.weixin.qq.com", cdn_resp);

    // 奇数长度 hex 字符串，hex_decode 会抛异常
    auto result = client->download_image("test_param", "abc");

    ASSERT_TRUE(result.empty(), "奇数长度 aes_key 时返回空 vector（hex_decode 异常被捕获）");
}

void test_download_image_invalid_aes_key_wrong_length() {
    test_header("download_image: aes_key 解码后非 16 字节");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 配置 CDN 返回有效的加密数据
    agent::HttpResponse cdn_resp;
    cdn_resp.status_code = 200;
    cdn_resp.body = std::string(32, 'x');
    mock.add_response("GET", "novac2c.cdn.weixin.qq.com", cdn_resp);

    // 8 字节密钥（hex 为 16 字符），不满足 AES-128 的 16 字节要求
    auto result = client->download_image("test_param", "0011223344556677");

    ASSERT_TRUE(result.empty(), "8 字节 aes_key 时返回空 vector");
}

void test_download_image_ciphertext_not_multiple_of_16() {
    test_header("download_image: 密文长度非 16 倍数");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 配置 CDN 返回 17 字节（非 16 倍数）
    agent::HttpResponse cdn_resp;
    cdn_resp.status_code = 200;
    cdn_resp.body = std::string(17, 'x');
    mock.add_response("GET", "novac2c.cdn.weixin.qq.com", cdn_resp);

    auto result = client->download_image("test_param", "00112233445566778899aabbccddeeff");

    ASSERT_TRUE(result.empty(), "非 16 倍数密文时返回空 vector（AES 解密异常被捕获）");
}

void test_download_image_pkcs7_invalid_padding() {
    test_header("download_image: PKCS7 去填充校验失败");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 构造 16 字节密文，但最后一个字节是 0x05（声称 5 字节填充）
    // 但前面的字节不是 0x05，PKCS7 校验会失败
    std::string fake_ciphertext(16, 0x00);
    fake_ciphertext[15] = 0x05;  // 声称 5 字节填充
    // 字节 11-14 是 0x00，不是 0x05，校验失败

    agent::HttpResponse cdn_resp;
    cdn_resp.status_code = 200;
    cdn_resp.body = fake_ciphertext;
    mock.add_response("GET", "novac2c.cdn.weixin.qq.com", cdn_resp);

    auto result = client->download_image("test_param", "00112233445566778899aabbccddeeff");

    ASSERT_TRUE(result.empty(), "PKCS7 校验失败时返回空 vector");
}

void test_download_image_success() {
    test_header("download_image: 正常解密成功");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 用 crypto_utils 生成密钥并加密数据
    auto aes_key = agent::util::generate_aes_key();
    std::string aes_key_hex = agent::util::hex_encode(aes_key);

    // 原始图片数据（模拟 PNG 头）
    std::vector<uint8_t> original_data = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  // PNG signature
        0x00, 0x00, 0x00, 0x0D,  // IHDR length
        0x49, 0x48, 0x44, 0x52,  // "IHDR"
        0x00, 0x00, 0x03, 0x20,  // width=800
        0x00, 0x00, 0x02, 0x58   // height=600
    };

    auto encrypted = agent::util::aes_128_ecb_encrypt(original_data, aes_key);
    std::string encrypted_str(encrypted.begin(), encrypted.end());

    // 配置 CDN 返回加密数据
    agent::HttpResponse cdn_resp;
    cdn_resp.status_code = 200;
    cdn_resp.body = encrypted_str;
    mock.add_response("GET", "novac2c.cdn.weixin.qq.com", cdn_resp);

    auto result = client->download_image("test_param", aes_key_hex);

    ASSERT_FALSE(result.empty(), "正常解密应返回非空数据");
    ASSERT_EQ(result.size(), original_data.size());
    ASSERT_TRUE(std::equal(result.begin(), result.end(), original_data.begin()),
                "解密数据与原始数据一致");
}

// ============================================================================
// 测试用例：send_image 异常处理
// ============================================================================

void test_send_image_file_not_exist() {
    test_header("send_image: 文件不存在");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    bool result = client->send_image("user_id", "Z:\\nonexistent\\image.png", "ctx_token");

    ASSERT_FALSE(result, "文件不存在时返回 false");
    ASSERT_EQ(mock.get_requests().size(), 0);
}

void test_send_image_get_upload_url_failure() {
    test_header("send_image: get_upload_url 失败（ret!=0）");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 创建临时图片文件
    std::string file_path = create_temp_image_file("fake_png_data");

    // 配置 getuploadurl 返回 ret=-1
    agent::HttpResponse upload_url_resp;
    upload_url_resp.status_code = 200;
    upload_url_resp.body = R"({"ret":-1,"msg":"auth failed"})";
    mock.add_response("POST", "getuploadurl", upload_url_resp);

    bool result = client->send_image("user_id", file_path, "ctx_token");

    ASSERT_FALSE(result, "get_upload_url ret!=0 时返回 false");
    ASSERT_EQ(mock.get_call_count("POST", "getuploadurl"), 1);
    ASSERT_EQ(mock.get_call_count("PUT", "novac2c.cdn.weixin.qq.com"), 0);

    delete_file(file_path);
}

void test_send_image_get_upload_url_missing_fields() {
    test_header("send_image: get_upload_url 返回缺少字段");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    std::string file_path = create_temp_image_file("fake_png_data");

    // 配置 getuploadurl 返回 ret=0 但缺少 upload_url 和 file_id
    agent::HttpResponse upload_url_resp;
    upload_url_resp.status_code = 200;
    upload_url_resp.body = R"({"ret":0,"msg":"ok"})";
    mock.add_response("POST", "getuploadurl", upload_url_resp);

    bool result = client->send_image("user_id", file_path, "ctx_token");

    ASSERT_FALSE(result, "缺少 upload_url/file_id 时返回 false");

    delete_file(file_path);
}

void test_send_image_cdn_put_failure() {
    test_header("send_image: CDN PUT 失败（非 2xx）");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    std::string file_path = create_temp_image_file("fake_png_data");

    // 配置 getuploadurl 成功
    agent::HttpResponse upload_url_resp;
    upload_url_resp.status_code = 200;
    upload_url_resp.body = R"({"ret":0,"upload_url":"https://novac2c.cdn.weixin.qq.com/c2c/put","file_id":"file_123","expire_seconds":3600})";
    mock.add_response("POST", "getuploadurl", upload_url_resp);

    // 配置 CDN PUT 失败
    agent::HttpResponse put_resp;
    put_resp.status_code = 403;
    put_resp.body = "Forbidden";
    mock.add_response("PUT", "novac2c.cdn.weixin.qq.com", put_resp);

    bool result = client->send_image("user_id", file_path, "ctx_token");

    ASSERT_FALSE(result, "CDN PUT 403 时返回 false");
    ASSERT_EQ(mock.get_call_count("PUT", "novac2c.cdn.weixin.qq.com"), 1);
    ASSERT_EQ(mock.get_call_count("POST", "sendmessage"), 0);

    delete_file(file_path);
}

void test_send_image_sendmessage_failure() {
    test_header("send_image: sendmessage 失败（ret!=0）");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    std::string file_path = create_temp_image_file("fake_png_data");

    // 配置 getuploadurl 成功
    agent::HttpResponse upload_url_resp;
    upload_url_resp.status_code = 200;
    upload_url_resp.body = R"({"ret":0,"upload_url":"https://novac2c.cdn.weixin.qq.com/c2c/put","file_id":"file_123","expire_seconds":3600})";
    mock.add_response("POST", "getuploadurl", upload_url_resp);

    // 配置 CDN PUT 成功
    agent::HttpResponse put_resp;
    put_resp.status_code = 200;
    put_resp.body = "";
    mock.add_response("PUT", "novac2c.cdn.weixin.qq.com", put_resp);

    // 配置 sendmessage 返回 ret=-1
    agent::HttpResponse sendmsg_resp;
    sendmsg_resp.status_code = 200;
    sendmsg_resp.body = R"({"ret":-1,"msg":"send failed"})";
    mock.add_response("POST", "sendmessage", sendmsg_resp);

    bool result = client->send_image("user_id", file_path, "ctx_token");

    ASSERT_FALSE(result, "sendmessage ret!=0 时返回 false");
    ASSERT_EQ(mock.get_call_count("POST", "sendmessage"), 1);

    delete_file(file_path);
}

void test_send_image_success() {
    test_header("send_image: 正常发送成功");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 创建有效的 PNG 文件（包含 PNG 签名和 IHDR）
    std::vector<uint8_t> png_data = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  // PNG signature
        0x00, 0x00, 0x00, 0x0D,  // IHDR length
        0x49, 0x48, 0x44, 0x52,  // "IHDR"
        0x00, 0x00, 0x03, 0x20,  // width=800
        0x00, 0x00, 0x02, 0x58,  // height=600
        0x08, 0x02, 0x00, 0x00, 0x00,  // bit depth, color type, etc.
    };
    std::string png_str(png_data.begin(), png_data.end());
    std::string file_path = create_temp_image_file(png_str, ".png");

    // 配置 getuploadurl 成功
    agent::HttpResponse upload_url_resp;
    upload_url_resp.status_code = 200;
    upload_url_resp.body = R"({"ret":0,"upload_url":"https://novac2c.cdn.weixin.qq.com/c2c/put","file_id":"file_123","expire_seconds":3600})";
    mock.add_response("POST", "getuploadurl", upload_url_resp);

    // 配置 CDN PUT 成功
    agent::HttpResponse put_resp;
    put_resp.status_code = 200;
    mock.add_response("PUT", "novac2c.cdn.weixin.qq.com", put_resp);

    // 配置 sendmessage 成功
    agent::HttpResponse sendmsg_resp;
    sendmsg_resp.status_code = 200;
    sendmsg_resp.body = R"({"ret":0,"msg":"ok"})";
    mock.add_response("POST", "sendmessage", sendmsg_resp);

    bool result = client->send_image("user_id", file_path, "ctx_token");

    ASSERT_TRUE(result, "正常流程应返回 true");
    ASSERT_EQ(mock.get_call_count("POST", "getuploadurl"), 1);
    ASSERT_EQ(mock.get_call_count("PUT", "novac2c.cdn.weixin.qq.com"), 1);
    ASSERT_EQ(mock.get_call_count("POST", "sendmessage"), 1);

    // 验证 sendmessage 请求体中包含图片尺寸和 type=2
    bool found_sendmsg = false;
    for (const auto& req : mock.get_requests()) {
        if (req.method == "POST" && req.url.find("sendmessage") != std::string::npos) {
            found_sendmsg = true;
            ASSERT_TRUE(req.body.find("\"type\":2") != std::string::npos ||
                        req.body.find("\"type\": 2") != std::string::npos,
                        "sendmessage body 包含 type=2");
            ASSERT_TRUE(req.body.find("\"width\":800") != std::string::npos ||
                        req.body.find("\"width\": 800") != std::string::npos,
                        "sendmessage body 包含正确的 width=800");
            ASSERT_TRUE(req.body.find("\"height\":600") != std::string::npos ||
                        req.body.find("\"height\": 600") != std::string::npos,
                        "sendmessage body 包含正确的 height=600");
            break;
        }
    }
    ASSERT_TRUE(found_sendmsg, "找到了 sendmessage 请求");

    delete_file(file_path);
}

void test_send_image_unauthenticated() {
    test_header("send_image: 未认证时返回 false");

    MockHttpClient mock;
    // 不调用 set_credentials，client 未认证
    IlinkClient client(mock);

    std::string file_path = create_temp_image_file("fake_data");

    bool result = client.send_image("user_id", file_path, "ctx_token");

    ASSERT_FALSE(result, "未认证时返回 false");
    ASSERT_EQ(mock.get_requests().size(), 0);

    delete_file(file_path);
}

void test_download_image_unauthenticated() {
    test_header("download_image: 未认证时不阻止调用（仅 HTTP 层鉴权）");

    MockHttpClient mock;
    // download_image 不检查 is_authenticated，直接调用 CDN
    // CDN 请求不携带 bot_token，只检查 HTTP 响应
    IlinkClient client(mock);

    agent::HttpResponse cdn_resp;
    cdn_resp.status_code = 403;
    cdn_resp.body = "Forbidden";
    mock.add_response("GET", "novac2c.cdn.weixin.qq.com", cdn_resp);

    auto result = client.download_image("test_param", "00112233445566778899aabbccddeeff");

    ASSERT_TRUE(result.empty(), "CDN 403 时返回空 vector");
}

// ============================================================================
// 测试用例：get_upload_url 异常处理
// ============================================================================

void test_get_upload_url_unauthenticated() {
    test_header("get_upload_url: 未认证时返回 nullopt");

    MockHttpClient mock;
    IlinkClient client(mock);

    auto result = client.get_upload_url("image/jpeg");

    ASSERT_FALSE(result.has_value(), "未认证时返回 nullopt");
    ASSERT_EQ(mock.get_requests().size(), 0);
}

void test_get_upload_url_network_error() {
    test_header("get_upload_url: HTTP 错误时返回 nullopt");

    MockHttpClient mock;
    auto client = create_authenticated_client(mock);

    // 配置 HTTP 500
    agent::HttpResponse resp;
    resp.status_code = 500;
    resp.body = "Internal Server Error";
    mock.add_response("POST", "getuploadurl", resp);

    auto result = client->get_upload_url("image/jpeg");

    ASSERT_FALSE(result.has_value(), "HTTP 500 时返回 nullopt");
}

// ============================================================================
// 主函数
// ============================================================================
int main() {
    std::cout << "========================================\n";
    std::cout << "  IlinkClient 图片下载和发送异常处理测试\n";
    std::cout << "========================================\n";

    // download_image 异常处理
    test_download_image_cdn_failure();
    test_download_image_empty_body();
    test_download_image_oversized_body();
    test_download_image_invalid_aes_key_odd_length();
    test_download_image_invalid_aes_key_wrong_length();
    test_download_image_ciphertext_not_multiple_of_16();
    test_download_image_pkcs7_invalid_padding();
    test_download_image_success();

    // download_image 认证
    test_download_image_unauthenticated();

    // send_image 异常处理
    test_send_image_file_not_exist();
    test_send_image_get_upload_url_failure();
    test_send_image_get_upload_url_missing_fields();
    test_send_image_cdn_put_failure();
    test_send_image_sendmessage_failure();
    test_send_image_success();
    test_send_image_unauthenticated();

    // get_upload_url 异常处理
    test_get_upload_url_unauthenticated();
    test_get_upload_url_network_error();

    // 汇总结果
    std::cout << "\n========================================\n";
    std::cout << "  Results: " << pass_count << " / " << test_count << " passed\n";
    std::cout << "========================================\n";

    return (pass_count == test_count) ? 0 : 1;
}
