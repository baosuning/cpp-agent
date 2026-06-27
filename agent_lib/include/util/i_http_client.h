#pragma once
#include <string>
#include <map>
#include <memory>

namespace agent {

struct HttpResponse {
    int          status_code = 0;
    std::string  body;
    bool         is_error = false;
    std::string  error_message;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    virtual HttpResponse post(const std::string& url,
                              const std::string& body,
                              const std::map<std::string, std::string>& headers,
                              int timeout_ms = 90000) = 0;

    virtual HttpResponse get(const std::string& url,
                             const std::map<std::string, std::string>& headers,
                             int timeout_ms = 90000) = 0;

    virtual HttpResponse put(const std::string& url,
                              const std::string& body,
                              const std::map<std::string, std::string>& headers,
                              int timeout_ms = 90000) = 0;

    virtual void set_debug(bool enable) = 0;
};

std::unique_ptr<IHttpClient> create_http_client();

} // namespace agent
