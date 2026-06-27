#include <agent/agent.h>
#include <agent/builtin_tools.h>
#include <agent/i_tool.h>
#include <agent/i_memory.h>
#include <util/log.h>
#include <util/i_http_client.h>
#include <nlohmann/json.hpp>
#include "app/agent_factory.h"
#include "channels/channel_factory.h"
#include "utils/utils.h"
#include "tools/echo_tool.h"
#include "tools/qr_code_tool.h"
#include "memory/simple_memory.h"
#ifdef _WIN32
#include <windows.h>
#endif

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <regex>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <csignal>

namespace fs = std::filesystem;

// WeChat 模式退出信号
static std::atomic<bool> g_should_stop{false};

static void signal_handler(int) {
    g_should_stop.store(true);
}

// 渠道模式入口（微信/飞书/企业微信/QQ 等统一入口）
static int run_channel_mode(const std::string& platform,
                            const fs::path& config_dir,
                            const char* api_key_env,
                            const std::string& mode_str,
                            bool debug) {
    AGENT_LOG_INFO("Main") << "========== Channel Mode Starting ==========";
    AGENT_LOG_INFO("Main") << "Platform: " << platform;
    AGENT_LOG_INFO("Main") << "Config dir: " << config_dir.string();
    AGENT_LOG_INFO("Main") << "Agent mode: " << (mode_str.empty() ? "(from config)" : mode_str);
    AGENT_LOG_INFO("Main") << "Debug: " << (debug ? "true" : "false");

    // 创建 Agent（复用 agent_factory）
    AGENT_LOG_INFO("Main") << "[1/4] Creating Agent via agent_factory...";
    auto build = agent_cli::create_agent(config_dir, api_key_env, mode_str, debug, true);
    if (!build.agent) {
        AGENT_LOG_ERROR("Main") << "Failed to create agent";
        return 1;
    }
    AGENT_LOG_INFO("Main") << "[1/4] Agent created, mode=" << build.mode_str
        << ", debug=" << (build.debug ? "true" : "false");

    if (debug) {
        agent::log::set_level(agent::log::Level::Debug);
    }

    // 创建 HTTP 客户端
    AGENT_LOG_INFO("Main") << "[2/4] Creating HTTP client...";
    auto http = agent::create_http_client();
    AGENT_LOG_INFO("Main") << "[2/4] HTTP client created";

    // 通过工厂创建渠道（平台无关）
    AGENT_LOG_INFO("Main") << "[3/4] Creating channel via factory, platform='" << platform << "'...";
    auto channel = agent_cli::channels::create_channel(platform, *http, "data/");
    if (!channel) {
        AGENT_LOG_ERROR("Main") << "Unsupported channel platform: " << platform
            << " (supported: wechat; feishu/wecom/qq reserved)";
        std::cerr << "Unsupported channel platform: " << platform << "\n"
                  << "Supported: wechat (feishu/wecom/qq reserved for future)\n";
        return 1;
    }
    AGENT_LOG_INFO("Main") << "[3/4] Channel created, implementation="
        << platform << " (IChannel subclass)";

    // 启动 Agent 引擎
    AGENT_LOG_INFO("Main") << "[4/4] Starting Agent engine...";
    build.agent->start();
    AGENT_LOG_INFO("Main") << "[4/4] Agent engine started";

    // 启动渠道
    AGENT_LOG_INFO("Main") << "Starting channel (platform=" << platform << ")...";
    if (!channel->start(*build.agent)) {
        AGENT_LOG_ERROR("Main") << "Failed to start " << platform << " channel";
        std::cerr << "Failed to start " << platform << " channel\n";
        build.agent->stop();
        return 1;
    }
    AGENT_LOG_INFO("Main") << "Channel started successfully (platform=" << platform << ")";

    // 注册 Ctrl+C 信号处理
    std::signal(SIGINT, signal_handler);

    AGENT_LOG_INFO("Main") << "========== Channel Mode Ready ==========";
    std::cout << platform << " channel running. Press Ctrl+C to stop.\n";

    // 等待退出信号
    while (!g_should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    AGENT_LOG_INFO("Main") << "========== Channel Mode Stopping ==========";
    std::cout << "\nStopping...\n";

    channel->stop();
    AGENT_LOG_INFO("Main") << "Channel stopped (platform=" << platform << ")";
    build.agent->stop();
    AGENT_LOG_INFO("Main") << "Agent engine stopped";

    // 打印 token 使用统计（必须在 channel->stop() 和 agent->stop() 之后，
    // 确保所有飞行中的 LLM 调用已结束，统计完整）
    {
        auto stats = build.agent->get_token_stats();
        std::cout << "\n--- Token Usage (cumulative) ---" << std::endl;
        std::cout << "  Prompt:     " << stats.total_prompt_tokens << std::endl;
        std::cout << "  Completion: " << stats.total_completion_tokens << std::endl;
        std::cout << "  Total:      " << stats.total_tokens << std::endl;
        std::cout << "  LLM calls:  " << stats.llm_call_count << std::endl;
        std::cout << "--------------------------------" << std::endl;
    }

    std::cout << "Agent stopped.\n";
    return 0;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    AGENT_LOG_INFO("Main") << "========== Agent CLI Starting ==========";

    const char* api_key_env = std::getenv("LLM_API_KEY");
    if (!api_key_env) {
        AGENT_LOG_ERROR("Main") << "Environment variable LLM_API_KEY is not set";
        return 1;
    }
    AGENT_LOG_INFO("Main") << "LLM_API_KEY detected (len=" << strlen(api_key_env) << ")";

    // 定位配置文件目录
    fs::path config_dir;
    const char* env_config_dir = std::getenv("AGENT_CONFIG_DIR");
    if (env_config_dir && *env_config_dir) {
        config_dir = fs::path(env_config_dir);
        AGENT_LOG_INFO("Main") << "Config dir from AGENT_CONFIG_DIR: " << config_dir.string();
    } else {
#ifdef _WIN32
        char exe_path[MAX_PATH];
        GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
        config_dir = fs::path(exe_path).parent_path() / "config";
#else
        config_dir = fs::path(__FILE__).parent_path() / "config";
#endif
        AGENT_LOG_INFO("Main") << "Config dir from exe path: " << config_dir.string();
    }

    // 命令行参数解析
    std::string mode_str;
    std::string channel_platform;  // 非空则进入渠道模式
    bool debug = false;
#ifdef _WIN32
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg(argv[i]);
            if (arg == L"--mode" && i + 1 < argc) {
                std::wstring mode_value(argv[i + 1]);
                if (mode_value == L"plan_execute") {
                    mode_str = "plan_execute";
                } else if (mode_value == L"reflection") {
                    mode_str = "reflection";
                } else if (mode_value == L"custom") {
                    mode_str = "custom";
                } else {
                    mode_str = "react";
                }
                AGENT_LOG_INFO("Main") << "Arg --mode = " << mode_str;
                ++i;
            } else if (arg == L"--debug") {
                debug = true;
                AGENT_LOG_INFO("Main") << "Arg --debug = true";
            } else if (arg == L"--wechat") {
                // --wechat 是 --channel wechat 的简写
                channel_platform = "wechat";
                AGENT_LOG_INFO("Main") << "Arg --wechat (alias for --channel wechat)";
            } else if (arg == L"--channel" && i + 1 < argc) {
                // 通用渠道参数：--channel wechat|feishu|wecom|qq
                std::wstring plat(argv[i + 1]);
                std::string s(plat.begin(), plat.end());
                channel_platform = s;
                AGENT_LOG_INFO("Main") << "Arg --channel = " << channel_platform;
                ++i;
            }
        }
        LocalFree(argv);
    }
#endif

    // 渠道模式（微信/飞书/企业微信/QQ）：统一入口
    if (!channel_platform.empty()) {
        AGENT_LOG_INFO("Main") << "Entering channel mode, platform=" << channel_platform;
        return run_channel_mode(channel_platform, config_dir, api_key_env, mode_str, debug);
    }

    // ========== CLI 模式：创建 Agent ==========
    AGENT_LOG_INFO("Main") << "Entering CLI interactive mode";
    AGENT_LOG_INFO("Main") << "[1/2] Creating Agent via agent_factory...";
    auto build = agent_cli::create_agent(config_dir, api_key_env, mode_str, debug);
    auto& agent_ptr = build.agent;
    debug = build.debug;

    if (!agent_ptr) {
        AGENT_LOG_ERROR("Main") << "Failed to create agent";
        return 1;
    }
    AGENT_LOG_INFO("Main") << "[1/2] Agent created, mode=" << build.mode_str
        << ", debug=" << (build.debug ? "true" : "false");

    if (debug) {
        agent::log::set_level(agent::log::Level::Debug);
    }

    // ========== 回调 ==========
    std::atomic<bool> agent_done{true};
    std::atomic<bool> agent_thinking{false};
    std::atomic<bool> agent_waiting_input{false};
    std::mutex cout_mutex;  // 保护 std::cout 输出，防止 Agent 输出和提示符交错
    std::mutex state_mutex;
    std::condition_variable state_cv;  // 替代轮询，立即响应状态变化

    agent_ptr->set_on_thinking_update([&cout_mutex, debug](const agent::ThinkingStep& step) {
        if (debug) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            if (step.tool_call) {
                auto tool_name = agent_cli::u8tostr(step.tool_call->name);
                auto args = agent_cli::u8tostr(step.tool_call->arguments);
                if (args.length() > 80) args = args.substr(0, 80) + "...";
                if (!args.empty() && args != "{}") {
                    std::cout << "  -> " << tool_name << "(" << args << ")" << std::endl;
                } else {
                    std::cout << "  -> " << tool_name << std::endl;
                }
                if (step.tool_result) {
                    auto result = agent_cli::u8tostr(step.tool_result->content);
                    if (result.length() > 150) result = result.substr(0, 150) + "...";
                    std::cout << "  <- " << result << std::endl;
                }
            } else {
                auto content = agent_cli::u8tostr(step.thinking_content);
                if (!content.empty()) std::cout << content << std::endl;
            }
        }
    });

    agent_ptr->set_on_output_ready([&](const agent::u8str& output) {
        auto str = agent_cli::u8tostr(output);
        if (!str.empty()) {
            // 过滤 DSML 格式的原始标签（模型以文本形式输出的 tool_call 标记）
            // 格式如 <｜｜DSML｜｜tool_calls> <｜｜DSML｜｜invoke> </｜｜DSML｜｜invoke> <｜｜DSML｜｜parameter> </｜｜DSML｜｜parameter>
            // 先移除整个 <...tool_calls>...</...tool_calls> 块（含内容）
            {
                // 匹配任意分隔符字符（ASCII | 或全角 ｜），不关心具体是什么
                std::regex block_regex(R"(<[^>]*tool_calls[^>]*>[\s\S]*?<\/[^>]*tool_calls[^>]*>)", std::regex::optimize);
                str = std::regex_replace(str, block_regex, "");
            }
            // 再移除残留的单个 DSML 标签（<...invoke>、<...parameter> 等）
            {
                std::regex tag_regex(R"(<[^>]*(?:tool_calls|invoke|parameter|/tool_calls|/invoke|/parameter)[^>]*>)", std::regex::optimize);
                str = std::regex_replace(str, tag_regex, "");
            }
            // 清理空行和多余空白
            {
                std::regex blank_lines_regex(R"(\n\s*\n)", std::regex::optimize);
                str = std::regex_replace(str, blank_lines_regex, "\n");
            }
            str = std::regex_replace(str, std::regex(R"(^\s+)"), "");
            str = std::regex_replace(str, std::regex(R"(\s+$)"), "");
            // 清理后如果只剩空白，不输出
            if (str.find_first_not_of(" \t\n\r") != std::string::npos) {
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << str << std::endl;
                std::cout.flush();
            }
        }
    });

    agent_ptr->set_on_state_change([&](agent::AgentState state) {
        switch (state) {
            case agent::AgentState::Thinking:
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    if (debug) std::cout << "[Thinking]" << std::endl;
                }
                agent_done = false;
                agent_thinking = true;
                agent_waiting_input = false;
                break;
            case agent::AgentState::WaitingToolResult:
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    if (debug) std::cout << "  [Running tool...]" << std::endl;
                }
                agent_done = false;
                agent_thinking = false;
                break;
            case agent::AgentState::WaitingUserConfirm:
                agent_done = false;
                agent_thinking = false;
                agent_waiting_input = true;
                // 通知主线程
                state_cv.notify_one();
                break;
            case agent::AgentState::Completed:
                // Loop 完成，但可能自动继续，不在此处触发提示符
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout.flush();
                }
                break;
            case agent::AgentState::Idle:
                // Agent 真正完成（不自动继续），可以显示提示符
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    std::cout.flush();

                    // PE 模式：输出执行摘要
                    if (debug) {
                        auto exec_log = agent_ptr->get_execution_log();
                        if (exec_log) {
                            std::cout << "\n=== Execution Summary ===" << std::endl;
                            std::cout << "  Status: " << agent_cli::u8tostr(exec_log->final_status) << std::endl;
                            std::cout << "  Steps: " << exec_log->step_logs.size() << std::endl;
                            std::cout << "  Replans: " << exec_log->replan_count << std::endl;
                            std::cout << "  Duration: " << exec_log->total_duration_ms << "ms" << std::endl;
                            for (const auto& slog : exec_log->step_logs) {
                                std::cout << "  [" << agent_cli::u8tostr(slog.step_id) << "] "
                                          << agent_cli::u8tostr(slog.status)
                                          << " (" << slog.duration_ms << "ms)"
                                          << " - " << agent_cli::u8tostr(slog.description).substr(0, 60)
                                          << std::endl;
                            }
                            std::cout << "=========================" << std::endl;
                        }
                    }

                    // Token 使用统计（默认显示，不限 debug 模式）
                    {
                        auto stats = agent_ptr->get_token_stats();
                        std::cout << "\n--- Token Usage (cumulative) ---" << std::endl;
                        std::cout << "  Prompt:     " << stats.total_prompt_tokens << std::endl;
                        std::cout << "  Completion: " << stats.total_completion_tokens << std::endl;
                        std::cout << "  Total:      " << stats.total_tokens << std::endl;
                        std::cout << "  LLM calls:  " << stats.llm_call_count << std::endl;
                        std::cout << "--------------------------------" << std::endl;
                    }
                }
                agent_done = true;
                agent_thinking = false;
                agent_waiting_input = false;
                // 通知主线程
                state_cv.notify_one();
                break;
            case agent::AgentState::Error:
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);
                    if (debug) std::cout << "[Error]" << std::endl;
                }
                agent_done = true;
                agent_thinking = false;
                agent_waiting_input = false;
                // 通知主线程
                state_cv.notify_one();
                break;
            default:
                break;
        }
    });

    // ========== 启动 Agent ==========
    AGENT_LOG_INFO("Main") << "[2/2] Starting Agent engine...";
    agent_ptr->start();
    AGENT_LOG_INFO("Main") << "[2/2] Agent engine started";
    AGENT_LOG_INFO("Main") << "========== CLI Mode Ready ==========";

    std::cout << "Agent started. Type your input (or 'quit' to exit):" << std::endl;

    bool running = true;
    while (running) {
        // 使用条件变量等待，替代轮询
        {
            std::unique_lock<std::mutex> lock(state_mutex);
            state_cv.wait(lock, [&] {
                return agent_done.load() || agent_waiting_input.load();
            });
        }

        agent_waiting_input = false;

        // 在锁内打印提示符，确保不会和 Agent 输出交错
        {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cout << ">>> " << std::flush;
        }

        agent::u8str line;
        while (true) {
            line = agent_cli::read_user_input_line("");
            if (line.empty()) {
                if (std::cin.eof() && !std::cin.bad()) {
                    running = false;
                    break;
                }
                // 空输入时重新显示提示符
                std::lock_guard<std::mutex> lock(cout_mutex);
                std::cout << ">>> " << std::flush;
                continue;
            }
            if (line.size() == 4 && line[0] == u8'q' && line[1] == u8'u'
                && line[2] == u8'i' && line[3] == u8't') {
                running = false;
                break;
            }
            break;
        }

        if (!running) break;

        agent_done = false;
        agent_ptr->submit_input(line);
    }

    AGENT_LOG_INFO("Main") << "========== CLI Mode Stopping ==========";
    agent_ptr->stop();
    AGENT_LOG_INFO("Main") << "Agent engine stopped";
    std::cout << "Agent stopped." << std::endl;

    return 0;
}
