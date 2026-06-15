#include <util/i_http_client.h>
#include <util/log.h>

#ifdef AGENT_WINDOWS
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include <memoryapi.h>
#endif

namespace agent {

#ifdef AGENT_WINDOWS

// 将指定代码页的字符串转换为 UTF-8
static std::string convert_codepage_to_utf8(const std::string& input, UINT codepage) {
    if (input.empty()) return input;

    int wlen = MultiByteToWideChar(codepage, 0, input.c_str(), -1, nullptr, 0);
    if (wlen == 0) return input;

    std::wstring wstr(wlen - 1, L'\0');
    MultiByteToWideChar(codepage, 0, input.c_str(), -1, &wstr[0], wlen);

    int utf8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8len == 0) return input;

    std::string result(utf8len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], utf8len, nullptr, nullptr);

    return result;
}

// 从 Content-Type 头中提取 charset 值（如 "text/html; charset=gbk" → "gbk"）
static std::string extract_charset_from_content_type(const std::string& content_type) {
    if (content_type.empty()) return {};

    std::string ct_lower = content_type;
    std::transform(ct_lower.begin(), ct_lower.end(), ct_lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    auto pos = ct_lower.find("charset=");
    if (pos == std::string::npos) return {};

    std::string charset = content_type.substr(pos + 8);
    // 去除尾部分号、空格等
    auto end = charset.find_first_of("; \t\r\n");
    if (end != std::string::npos) {
        charset = charset.substr(0, end);
    }
    // 去除引号
    if (!charset.empty() && (charset.front() == '"' || charset.front() == '\'')) {
        charset = charset.substr(1);
    }
    if (!charset.empty() && (charset.back() == '"' || charset.back() == '\'')) {
        charset.pop_back();
    }
    return charset;
}

// 从 HTML 的 <meta charset> 或 <meta http-equiv> 中提取编码声明
static std::string extract_charset_from_html(const std::string& body) {
    if (body.empty()) return {};

    std::string body_lower = body;
    std::transform(body_lower.begin(), body_lower.end(), body_lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // 只搜索前 4096 字节，避免大文件开销
    std::string head = body_lower.substr(0, std::min<size_t>(body_lower.size(), 4096));

    // 方式1: <meta charset="gbk">
    auto meta_pos = head.find("<meta");
    if (meta_pos != std::string::npos) {
        auto charset_pos = head.find("charset=", meta_pos);
        if (charset_pos != std::string::npos && charset_pos < meta_pos + 512) {
            std::string charset = body_lower.substr(charset_pos + 8);
            auto end = charset.find_first_of("\"' ;\t\r\n>");
            if (end != std::string::npos) {
                charset = charset.substr(0, end);
            }
            return charset;
        }
    }

    return {};
}

// 根据 charset 名称判断是否需要 GBK 转换
static bool is_gbk_charset(const std::string& charset) {
    std::string cs_lower = charset;
    std::transform(cs_lower.begin(), cs_lower.end(), cs_lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return cs_lower == "gbk" || cs_lower == "gb2312" || cs_lower == "gb18030";
}

static std::string detect_and_convert_to_utf8(const std::string& body, const std::string& content_type) {
    if (body.empty()) return body;

    // 1. 优先从 Content-Type 响应头获取 charset
    std::string charset = extract_charset_from_content_type(content_type);

    // 2. 如果响应头未声明，从 HTML meta 标签中检测
    if (charset.empty()) {
        charset = extract_charset_from_html(body);
    }

    // 3. 如果是 GBK 系列编码，转换为 UTF-8
    if (is_gbk_charset(charset)) {
        return convert_codepage_to_utf8(body, 936);
    }

    return body;
}

class WinHttpClient : public IHttpClient {
public:
    WinHttpClient() {
        session_ = WinHttpOpen(
            L"AgentFramework/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0);

        if (session_) {
            DWORD resolve_timeout = 30000;
            WinHttpSetOption(session_, WINHTTP_OPTION_RESOLVE_TIMEOUT,
                            &resolve_timeout, sizeof(resolve_timeout));
        }
    }

    ~WinHttpClient() override {
        if (session_) {
            WinHttpCloseHandle(session_);
        }
    }

    HttpResponse post(const std::string& url, const std::string& body,
                      const std::map<std::string, std::string>& headers,
                      int timeout_ms) override {
        return do_request("POST", url, body, headers, timeout_ms);
    }

    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers,
                     int timeout_ms) override {
        return do_request("GET", url, "", headers, timeout_ms);
    }

    void set_debug(bool enable) override {
        debug_ = enable;
    }

private:
    HINTERNET session_;
    bool debug_ = false;

    HttpResponse do_request(const std::string& method, const std::string& url,
                           const std::string& body,
                           const std::map<std::string, std::string>& headers,
                           int timeout_ms) {
        HttpResponse response;

        if (!session_) {
            response.is_error = true;
            response.error_message = "Failed to create WinHTTP session";
            if (debug_) AGENT_LOG_DEBUG("WinHttp") << "Error: " << response.error_message;          return response;
        }

        std::string host;
        std::string path;
        int port = 0;
        bool is_https = false;

        if (!parse_url(url, host, path, port, is_https)) {
            response.is_error = true;
            response.error_message = "Failed to parse URL: " + url;
            if (debug_) AGENT_LOG_DEBUG("WinHttp") << "Error: " << response.error_message;          return response;
        }

        std::wstring whost(host.begin(), host.end());
        std::wstring wpath(path.begin(), path.end());

        if (debug_) {
            std::cout << "[WinHttp] Connecting to host=\"" << host << "\" port=" << port
                      << " path=\"" << path << "\" is_https=" << is_https
                      << " body_size=" << body.size() << " timeout_ms=" << timeout_ms << std::endl;
        }

        HINTERNET connect = WinHttpConnect(session_, whost.c_str(),
                                           static_cast<INTERNET_PORT>(port), 0);
        if (!connect) {
            DWORD err = GetLastError();
            response.is_error = true;
            response.error_message = "Failed to connect to " + host + " (error " + std::to_string(err) + ")";
            if (debug_) AGENT_LOG_DEBUG("WinHttp") << "Error: " << response.error_message;          return response;
        }

        DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
        std::wstring wmethod(method.begin(), method.end());

        HINTERNET request = WinHttpOpenRequest(connect, wmethod.c_str(),
                                               wpath.c_str(), nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               flags);
        if (!request) {
            DWORD err = GetLastError();
            WinHttpCloseHandle(connect);
            response.is_error = true;
            response.error_message = "Failed to create request (error " + std::to_string(err) + ")";
            if (debug_) AGENT_LOG_DEBUG("WinHttp") << "Error: " << response.error_message;          return response;
        }

        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
                        | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURE_PROTOCOLS,
                        &protocols, sizeof(protocols));

        DWORD ignore_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA
                           | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
                           | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
                           | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS,
                        &ignore_flags, sizeof(ignore_flags));

        if (timeout_ms > 0) {
            DWORD timeout = static_cast<DWORD>(timeout_ms);
            WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_TIMEOUT,
                            &timeout, sizeof(timeout));
            WinHttpSetOption(request, WINHTTP_OPTION_SEND_TIMEOUT,
                            &timeout, sizeof(timeout));
            WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_TIMEOUT,
                            &timeout, sizeof(timeout));
        }

        std::wstring header_str;
        for (const auto& [key, value] : headers) {
            std::wstring wkey(key.begin(), key.end());
            std::wstring wvalue(value.begin(), value.end());
            header_str += wkey + L": " + wvalue + L"\r\n";
        }

        // Only add Content-Type if not already set by caller, and only for POST with body
        bool has_content_type = false;
        for (const auto& [key, value] : headers) {
            if (key == "Content-Type" || key == "content-type") {
                has_content_type = true;
                break;
            }
        }
        if (!has_content_type && method == "POST" && !body.empty()) {
            header_str += L"Content-Type: application/json\r\n";
        }

        BOOL result = WinHttpSendRequest(request,
                                         header_str.c_str(),
                                         static_cast<DWORD>(-1),
                                         body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<void*>(static_cast<const void*>(body.data())),
                                         body.empty() ? 0 : static_cast<DWORD>(body.size()),
                                         body.empty() ? 0 : static_cast<DWORD>(body.size()),
                                         0);
        if (!result) {
            DWORD err = GetLastError();
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            response.is_error = true;
            response.error_message = "Failed to send request (error " + std::to_string(err) + ")";
            if (debug_) AGENT_LOG_DEBUG("WinHttp") << "Error: " << response.error_message;          return response;
        }

        result = WinHttpReceiveResponse(request, nullptr);
        if (!result) {
            DWORD err = GetLastError();
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            response.is_error = true;
            response.error_message = "Failed to receive response (error " + std::to_string(err) + ")";
            if (debug_) AGENT_LOG_DEBUG("WinHttp") << "Error: " << response.error_message;          return response;
        }

        DWORD status_code = 0;
        DWORD status_code_size = sizeof(status_code);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_size,
                           WINHTTP_NO_HEADER_INDEX);
        response.status_code = static_cast<int>(status_code);

        DWORD bytes_available = 0;
        DWORD bytes_read = 0;
        std::string response_body;

        do {
            bytes_available = 0;
            if (!WinHttpQueryDataAvailable(request, &bytes_available)) {
                break;
            }

            if (bytes_available == 0) {
                break;
            }

            std::vector<char> buffer(bytes_available + 1);
            if (WinHttpReadData(request, buffer.data(), bytes_available, &bytes_read)) {
                response_body.append(buffer.data(), bytes_read);
            }
        } while (bytes_available > 0);

        std::string content_type_header;
        DWORD content_type_size = 0;
        WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                           nullptr, &content_type_size, WINHTTP_NO_HEADER_INDEX);
        if (content_type_size > 0) {
            std::vector<wchar_t> ct_buffer(content_type_size / sizeof(wchar_t) + 1);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                               ct_buffer.data(), &content_type_size, WINHTTP_NO_HEADER_INDEX);
            std::string ct(ct_buffer.begin(), ct_buffer.end());
            content_type_header = ct;
        }

        response.body = detect_and_convert_to_utf8(response_body, content_type_header);

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);

        return response;
    }

    static bool parse_url(const std::string& url, std::string& host,
                          std::string& path, int& port, bool& is_https) {
        is_https = false;
        port = 0;

        // 截断 URL fragment (# 之后的部分)，服务端请求不需要 fragment
        std::string clean_url = url;
        size_t hash_pos = clean_url.find('#');
        if (hash_pos != std::string::npos) {
            clean_url = clean_url.substr(0, hash_pos);
        }

        size_t pos = 0;

        if (clean_url.compare(0, 8, "https://") == 0) {
            is_https = true;
            port = 443;
            pos = 8;
        } else if (clean_url.compare(0, 7, "http://") == 0) {
            is_https = false;
            port = 80;
            pos = 7;
        } else {
            return false;
        }

        size_t path_pos = clean_url.find('/', pos);
        std::string authority = (path_pos != std::string::npos)
                                ? clean_url.substr(pos, path_pos - pos)
                                : clean_url.substr(pos);

        size_t colon_pos = authority.find(':');
        if (colon_pos != std::string::npos) {
            host = authority.substr(0, colon_pos);
            port = std::stoi(authority.substr(colon_pos + 1));
        } else {
            host = authority;
        }

        path = (path_pos != std::string::npos) ? clean_url.substr(path_pos) : "/";

        return !host.empty();
    }
};

#else

class StubHttpClient : public IHttpClient {
public:
    HttpResponse post(const std::string& url, const std::string& body,
                      const std::map<std::string, std::string>& headers,
                      int timeout_ms) override {
        (void)timeout_ms;
        return {0, "", true, "HTTP client not implemented on this platform"};
    }
    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers,
                     int timeout_ms) override {
        (void)timeout_ms;
        return {0, "", true, "HTTP client not implemented on this platform"};
    }
};

#endif

std::unique_ptr<IHttpClient> create_http_client() {
#ifdef AGENT_WINDOWS
    return std::make_unique<WinHttpClient>();
#else
    return std::make_unique<StubHttpClient>();
#endif
}

} // namespace agent
