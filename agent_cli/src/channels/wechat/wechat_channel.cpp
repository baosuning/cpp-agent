// agent_cli/src/channels/wechat_channel.cpp
// 微信渠道编排层实现

#include "wechat_channel.h"
#include <util/log.h>
#include <util/u8str_utils.h>
#include <qrcodegen/qrcodegen.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;
namespace agent_cli::channels::wechat {

using json = nlohmann::json;

WeChatChannel::WeChatChannel(std::unique_ptr<IlinkClient> client, WeChatChannelConfig config)
    : client_(std::move(client)), config_(std::move(config)) {}

WeChatChannel::~WeChatChannel() {
    stop();
}

// ===== 持久化 =====

static std::string current_iso_time() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

bool WeChatChannel::save_session(const LoginResult& login) {
    try {
        fs::path p(config_.session_file);
        fs::create_directories(p.parent_path());

        json j;
        j["bot_token"] = login.bot_token;
        j["base_url"] = login.base_url;
        j["ilink_user_id"] = login.ilink_user_id;
        j["ilink_bot_id"] = login.ilink_bot_id;
        j["saved_at"] = current_iso_time();

        std::ofstream f(p);
        f << j.dump(2);
        AGENT_LOG_INFO("WeChatChannel") << "Session saved to " << config_.session_file;
        return true;
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("WeChatChannel") << "save_session failed: " << e.what();
        return false;
    }
}

std::optional<LoginResult> WeChatChannel::load_session() {
    try {
        fs::path p(config_.session_file);
        if (!fs::exists(p)) return std::nullopt;

        std::ifstream f(p);
        json j;
        f >> j;

        LoginResult lr;
        lr.bot_token = j.value("bot_token", "");
        lr.base_url = j.value("base_url", "");
        lr.ilink_user_id = j.value("ilink_user_id", "");
        lr.ilink_bot_id = j.value("ilink_bot_id", "");
        if (lr.bot_token.empty()) return std::nullopt;

        AGENT_LOG_INFO("WeChatChannel") << "Session loaded from " << config_.session_file;
        return lr;
    } catch (const std::exception& e) {
        AGENT_LOG_WARN("WeChatChannel") << "load_session failed: " << e.what();
        return std::nullopt;
    }
}

bool WeChatChannel::save_sync_buf(const std::string& buf) {
    try {
        fs::path p(config_.sync_buf_file);
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << buf;
        return true;
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("WeChatChannel") << "save_sync_buf failed: " << e.what();
        return false;
    }
}

std::string WeChatChannel::load_sync_buf() {
    try {
        fs::path p(config_.sync_buf_file);
        if (!fs::exists(p)) return {};
        std::ifstream f(p);
        std::string buf((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        return buf;
    } catch (const std::exception& e) {
        AGENT_LOG_WARN("WeChatChannel") << "load_sync_buf failed: " << e.what();
        return {};
    }
}

// ===== 二维码显示 =====

namespace {
    // 判断二维码模块是否黑色；超出边界视为白色（用于 quiet zone）
    bool qr_module(const qrcodegen::QrCode& qr, int x, int y) {
        int size = qr.getSize();
        if (x < 0 || y < 0 || x >= size || y >= size) return false;
        return qr.getModule(x, y);
    }

    // 将 QR 码保存为 BMP 图片（Windows 原生支持，无需第三方压缩库）
    // module_px: 每个模块的像素边长；border: 四周 quiet zone 的模块数
    bool save_qr_as_bmp(const qrcodegen::QrCode& qr, const std::string& path,
                        int module_px, int border_modules) {
        int size = qr.getSize();
        int pixel_width = (size + 2 * border_modules) * module_px;
        // BMP 每行像素必须是 4 字节对齐
        int row_bytes = ((pixel_width * 3 + 3) / 4) * 4;
        int pixel_data_size = row_bytes * pixel_width;

        // BMP 文件头 (14 bytes) + BITMAPINFOHEADER (40 bytes)
        std::vector<unsigned char> file;
        file.reserve(14 + 40 + pixel_data_size);

        // BMP 文件头
        file.push_back('B'); file.push_back('M');
        uint32_t file_size = 14 + 40 + pixel_data_size;
        for (int i = 0; i < 4; ++i) file.push_back((file_size >> (i * 8)) & 0xFF);
        uint32_t reserved = 0;
        for (int i = 0; i < 4; ++i) file.push_back((reserved >> (i * 8)) & 0xFF);
        uint32_t pixel_offset = 14 + 40;
        for (int i = 0; i < 4; ++i) file.push_back((pixel_offset >> (i * 8)) & 0xFF);

        // BITMAPINFOHEADER
        uint32_t header_size = 40;
        for (int i = 0; i < 4; ++i) file.push_back((header_size >> (i * 8)) & 0xFF);
        for (int i = 0; i < 4; ++i) file.push_back((pixel_width >> (i * 8)) & 0xFF);
        for (int i = 0; i < 4; ++i) file.push_back((pixel_width >> (i * 8)) & 0xFF);
        uint16_t planes = 1;  for (int i = 0; i < 2; ++i) file.push_back((planes >> (i * 8)) & 0xFF);
        uint16_t bpp = 24;    for (int i = 0; i < 2; ++i) file.push_back((bpp >> (i * 8)) & 0xFF);
        uint32_t compression = 0;
        for (int i = 0; i < 4; ++i) file.push_back((compression >> (i * 8)) & 0xFF);
        uint32_t data_size = pixel_data_size;
        for (int i = 0; i < 4; ++i) file.push_back((data_size >> (i * 8)) & 0xFF);
        int32_t x_ppm = 2835; // 72 dpi
        for (int i = 0; i < 4; ++i) file.push_back((x_ppm >> (i * 8)) & 0xFF);
        for (int i = 0; i < 4; ++i) file.push_back((x_ppm >> (i * 8)) & 0xFF);
        uint32_t colors_used = 0;
        for (int i = 0; i < 4; ++i) file.push_back((colors_used >> (i * 8)) & 0xFF);
        uint32_t important = 0;
        for (int i = 0; i < 4; ++i) file.push_back((important >> (i * 8)) & 0xFF);

        // 像素数据：BMP 是自下而上，且 BGR 顺序
        std::vector<unsigned char> row(row_bytes, 0xFF);
        for (int y = 0; y < pixel_width; ++y) {
            int module_y = y / module_px - border_modules;
            for (int x = 0; x < pixel_width; ++x) {
                int module_x = x / module_px - border_modules;
                if (qr_module(qr, module_x, module_y)) {
                    int idx = x * 3;
                    row[idx + 0] = 0x00; // B
                    row[idx + 1] = 0x00; // G
                    row[idx + 2] = 0x00; // R
                } else {
                    int idx = x * 3;
                    row[idx + 0] = 0xFF;
                    row[idx + 1] = 0xFF;
                    row[idx + 2] = 0xFF;
                }
            }
            file.insert(file.end(), row.begin(), row.end());
        }

        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(file.data()), file.size());
        return f.good();
    }
}

bool WeChatChannel::save_and_show_qrcode(const std::string& image_url) {
    try {
        AGENT_LOG_INFO("WeChatChannel") << "Encoding QR code from URL: " << image_url;

        // qrcode_img_content 是要编码进二维码的内容（URL 字符串），用 qrcodegen 编码
        auto qr = qrcodegen::QrCode::encodeText(image_url.c_str(),
                                                qrcodegen::QrCode::Ecc::MEDIUM);

        // 同时提供终端打印和 BMP 图片两种方式
        std::cout << "\n========================================\n";
        std::cout << "Scan the QR code below with WeChat app:\n";
        std::cout << "========================================\n";
        int qr_size = qr.getSize();
        int border = 2; // 模块级 quiet zone
        for (int y = -border; y < qr_size + border; ++y) {
            for (int x = -border; x < qr_size + border; ++x) {
                bool black = qr_module(qr, x, y);
                std::cout << (black ? "██" : "  ");
            }
            std::cout << "\n";
        }
        std::cout << "========================================\n";
        std::cout << "If scanning fails, open: " << image_url << "\n";
        std::cout << "========================================\n\n";
        std::cout.flush();

        // 保存为 BMP 图片，避免终端字体/字符宽高比问题导致无法扫码
        fs::path bmp_path = fs::path(config_.qrcode_file).parent_path() / "qrcode.bmp";
        fs::create_directories(bmp_path.parent_path());
        if (!save_qr_as_bmp(qr, bmp_path.string(), 8, 4)) {
            AGENT_LOG_ERROR("WeChatChannel") << "Failed to save QR BMP: " << bmp_path.string();
            return false;
        }

#ifdef _WIN32
        // 关闭可能遗留的上一次二维码图片查看器
        if (qrcode_process_id_ != 0) {
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(qrcode_process_id_));
            if (hProcess != nullptr) {
                TerminateProcess(hProcess, 0);
                CloseHandle(hProcess);
            }
            qrcode_process_id_ = 0;
        }

        if (config_.auto_open_qrcode) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, bmp_path.string().c_str(), -1, nullptr, 0);
            std::wstring wpath(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, bmp_path.string().c_str(), -1, wpath.data(), wlen);

            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            sei.lpVerb = L"open";
            sei.lpFile = wpath.c_str();
            sei.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&sei) && sei.hProcess != nullptr) {
                qrcode_process_id_ = static_cast<uint32_t>(GetProcessId(sei.hProcess));
                CloseHandle(sei.hProcess);
            } else {
                AGENT_LOG_WARN("WeChatChannel") << "Failed to open QR code viewer";
            }
        }
#endif
        return true;
    } catch (const std::exception& e) {
        AGENT_LOG_ERROR("WeChatChannel") << "save_and_show_qrcode failed: " << e.what();
        return false;
    }
}

void WeChatChannel::on_login_success() {
#ifdef _WIN32
    if (qrcode_process_id_ != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(qrcode_process_id_));
        if (hProcess != nullptr) {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
        }
        qrcode_process_id_ = 0;
    }
#endif
    std::cout << "\n========================================\n";
    std::cout << "WeChat login successful!\n";
    std::cout << "Channel is ready. Waiting for messages...\n";
    std::cout << "========================================\n\n";
    std::cout.flush();
}

// ===== 登录流程 =====

bool WeChatChannel::do_login() {
    // 二维码可能过期需要重新获取，用外层循环
    while (running_.load()) {
        auto qr = client_->get_login_qrcode();
        if (!qr) {
            AGENT_LOG_ERROR("WeChatChannel") << "Failed to get login qrcode";
            return false;
        }

        if (!save_and_show_qrcode(qr->image_url)) {
            return false;
        }

        // 轮询扫码状态（每次二维码 2 分钟超时）
        auto start = std::chrono::steady_clock::now();
        while (running_.load()) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 120) {
                AGENT_LOG_ERROR("WeChatChannel") << "Login timeout (120s)";
                std::cout << "Login timeout. Please restart.\n";
                return false;
            }

            auto result = client_->poll_qrcode_status(qr->qrcode_id);
            if (result) {
                // 检测过期标记
                if (result->bot_token == "__EXPIRED__") {
                    AGENT_LOG_WARN("WeChatChannel") << "QR code expired, fetching new one...";
                    std::cout << "QR code expired, generating new one...\n";
                    break;  // 跳出内层循环，重新获取二维码
                }
                // confirmed
                if (!save_session(*result)) {
                    AGENT_LOG_WARN("WeChatChannel") << "Failed to save session";
                }
                client_->set_credentials(result->bot_token, result->base_url,
                                         result->ilink_user_id, result->ilink_bot_id);
                on_login_success();
                return true;
            }
            // pending / scanned，继续等待
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    return false;
}

// ===== 生命周期 =====

bool WeChatChannel::start(agent::Agent& agent) {
    if (running_.load()) return true;
    agent_ = &agent;

    // 注册 Agent 状态回调：到达 Idle/Error 时通知处理线程
    agent_->set_on_state_change([this](agent::AgentState state) {
        if (state == agent::AgentState::Idle || state == agent::AgentState::Error) {
            agent_idle_.store(true);
            state_cv_.notify_one();
        }
    });

    // 登录：优先复用 session
    running_.store(true);  // do_login 的轮询循环依赖此标志，需先置 true
    auto session = load_session();
    if (session && !session->ilink_user_id.empty()) {
        client_->set_credentials(session->bot_token, session->base_url,
                                 session->ilink_user_id, session->ilink_bot_id);
        AGENT_LOG_INFO("WeChatChannel") << "Reusing saved session";
        std::cout << "Reusing saved WeChat session.\n";
    } else {
        if (session) {
            AGENT_LOG_WARN("WeChatChannel") << "Saved session missing ilink_user_id, forcing re-login";
            std::cout << "Session incomplete, please re-scan QR code.\n";
        }
        if (!do_login()) {
            running_.store(false);  // 登录失败，重置标志
            return false;
        }
    }

    poll_thread_ = std::thread([this] { poll_loop(); });
    process_thread_ = std::thread([this] { process_loop(); });

    std::cout << "WeChat channel started. Waiting for messages...\n";
    return true;
}

void WeChatChannel::stop() {
    if (!running_.exchange(false)) return;

    queue_cv_.notify_all();
    state_cv_.notify_all();

    if (poll_thread_.joinable()) poll_thread_.join();
    if (process_thread_.joinable()) process_thread_.join();

    AGENT_LOG_INFO("WeChatChannel") << "Channel stopped";
}

// ===== 长轮询主循环 =====

void WeChatChannel::poll_loop() {
    std::string sync_buf = load_sync_buf();

    while (running_.load()) {
        auto result = client_->get_updates(sync_buf);
        if (!result) {
            AGENT_LOG_WARN("WeChatChannel") << "get_updates failed, retry in 5s";
            // 退出前等待，允许 stop() 中断
            for (int i = 0; i < 50 && running_.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        // 更新游标
        sync_buf = result->new_sync_buf;
        save_sync_buf(sync_buf);

        // 推送消息到队列
        if (!result->messages.empty()) {
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                for (auto& msg : result->messages) {
                    msg_queue_.push(std::move(msg));
                }
            }
            queue_cv_.notify_all();
        }
        // 无消息（长轮询超时）或处理完后，继续下一轮长轮询
    }
}

// ===== 消息处理主循环 =====

void WeChatChannel::process_loop() {
    while (running_.load()) {
        InboundMessage msg;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !msg_queue_.empty() || !running_.load(); });
            if (!running_.load()) return;
            msg = std::move(msg_queue_.front());
            msg_queue_.pop();
        }
        try {
            handle_message(msg);
        } catch (const std::exception& e) {
            AGENT_LOG_ERROR("WeChatChannel") << "handle_message exception: " << e.what();
            try {
                reply(std::string("Internal error: ") + e.what());
            } catch (...) {}
        }
    }
}

void WeChatChannel::handle_message(const InboundMessage& msg) {
    // 只处理文本消息
    std::string user_text;
    bool has_text = false;
    for (const auto& item : msg.items) {
        if (item.type == 1 && item.text) {
            user_text += *item.text;
            has_text = true;
        }
    }
    if (!has_text) {
        AGENT_LOG_INFO("WeChatChannel") << "Non-text message from " << msg.from_user_id << ", skipping";
        reply("Sorry, only text messages are supported in this version.");
        return;
    }

    if (user_text.empty()) {
        AGENT_LOG_INFO("WeChatChannel") << "Empty text message, skipping";
        return;
    }

    AGENT_LOG_INFO("WeChatChannel") << "Processing message from " << msg.from_user_id
        << " (len=" << user_text.size() << ")";

    // 设置当前消息上下文
    {
        std::lock_guard<std::mutex> lock(current_msg_mutex_);
        current_context_token_ = msg.context_token;
        current_from_user_id_ = msg.from_user_id;
        agent_idle_.store(false);
    }

    // 提交给 Agent
    agent_->submit_input(agent::u8str_util::to_u8str(user_text));

    // 启动 typing 线程（处理期间定期发送 typing）
    std::thread typing_thread;
    if (config_.enable_typing) {
        typing_thread = std::thread([this, token = msg.context_token] {
            run_typing_during_processing(token);
        });
    }

    // 等待 Agent 到达 Idle/Error
    {
        std::unique_lock<std::mutex> lock(current_msg_mutex_);
        state_cv_.wait(lock, [this] { return agent_idle_.load(); });
    }

    // 停止 typing 线程
    agent_idle_.store(true);  // 确保typing线程退出
    if (typing_thread.joinable()) typing_thread.join();

    // 获取最终输出并回复
    auto output_opt = agent_->get_output();
    std::string reply_text;
    if (output_opt && !output_opt->empty()) {
        reply_text = agent::u8str_util::to_string(*output_opt);
    } else {
        reply_text = "(Agent produced no output)";
        AGENT_LOG_WARN("WeChatChannel") << "Agent produced no output";
    }

    // 过滤 DSML 等控制标签（复用 main.cpp 的过滤逻辑思路）
    // 简单处理：移除 <...tool_calls...> 块和残留标签
    // 完整过滤逻辑较长，这里做基本清理
    {
        std::regex block_re(R"(<[^>]*tool_calls[^>]*>[\s\S]*?<\/[^>]*tool_calls[^>]*>)");
        reply_text = std::regex_replace(reply_text, block_re, "");
        std::regex tag_re(R"(<[^>]*(?:tool_calls|invoke|parameter|/tool_calls|/invoke|/parameter)[^>]*>)");
        reply_text = std::regex_replace(reply_text, tag_re, "");
        // 清理首尾空白
        auto first = reply_text.find_first_not_of(" \t\n\r");
        auto last = reply_text.find_last_not_of(" \t\n\r");
        if (first == std::string::npos) {
            reply_text = "(empty output after filtering)";
        } else {
            reply_text = reply_text.substr(first, last - first + 1);
        }
    }

    reply(reply_text);
}

void WeChatChannel::run_typing_during_processing(const std::string& context_token) {
    auto ticket_opt = client_->get_typing_ticket();
    if (!ticket_opt) {
        AGENT_LOG_DEBUG("WeChatChannel") << "No typing_ticket, skipping typing";
        return;
    }
    const std::string& ticket = *ticket_opt;

    while (!agent_idle_.load() && running_.load()) {
        client_->send_typing(context_token, ticket, 1);  // 1 = 开始输入
        // 分段 sleep 以便快速响应退出
        for (int i = 0; i < config_.typing_interval_ms / 100 && !agent_idle_.load() && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// 将 end 回退到 UTF-8 字符边界（确保不在多字节字符中间截断）
// 返回调整后的 end 值，保证 text[pos, end) 是完整的 UTF-8 序列
static size_t adjust_to_utf8_boundary(const std::string& text, size_t pos, size_t end) {
    if (end >= text.size()) return text.size();
    if (end <= pos) return pos;
    // UTF-8 后续字节范围 0x80-0xBF，首字节不在该范围
    // 向前回退直到 end 指向一个首字节或 ASCII 字节
    while (end > pos && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return end;
}

void WeChatChannel::reply(const std::string& text) {
    std::string token;
    std::string to_user;
    {
        std::lock_guard<std::mutex> lock(current_msg_mutex_);
        token = current_context_token_;
        to_user = current_from_user_id_;
    }
    if (token.empty() || to_user.empty()) {
        AGENT_LOG_WARN("WeChatChannel") << "reply: no current message context";
        return;
    }

    // 微信消息长度限制：单条文本约 2000 字符，超长分段发送
    const size_t kMaxChunk = 1800;
    if (text.size() <= kMaxChunk) {
        if (!client_->send_text(to_user, text, token)) {
            AGENT_LOG_ERROR("WeChatChannel") << "send_text failed (len=" << text.size() << ")";
        }
    } else {
        // 按 kMaxChunk 分段，尽量在换行处切分，并保证不在 UTF-8 字符中间截断
        size_t pos = 0;
        int part = 1;
        while (pos < text.size()) {
            size_t end = std::min(pos + kMaxChunk, text.size());
            // 尝试在换行处切分
            if (end < text.size()) {
                size_t nl = text.rfind('\n', end);
                if (nl != std::string::npos && nl > pos) end = nl + 1;
            }
            // 确保 end 在 UTF-8 字符边界上，避免多字节字符被截断
            end = adjust_to_utf8_boundary(text, pos, end);
            std::string chunk = text.substr(pos, end - pos);
            if (text.size() > kMaxChunk) {
                chunk = "[" + std::to_string(part++) + "] " + chunk;
            }
            if (!client_->send_text(to_user, chunk, token)) {
                AGENT_LOG_ERROR("WeChatChannel") << "send_text chunk failed at pos " << pos;
            }
            pos = end;
            // 分段发送间隔，避免频率限制
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    AGENT_LOG_INFO("WeChatChannel") << "Replied to " << to_user << " (len=" << text.size() << ")";
}

} // namespace agent_cli::channels::wechat
