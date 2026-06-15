#include <agent/agent.h>
#include <agent/builtin_tools.h>
#include <agent/i_tool.h>
#include <agent/i_memory.h>
#include <util/log.h>
#include <nlohmann/json.hpp>
#include "web_search/web_search_impl.h"
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

namespace fs = std::filesystem;

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    const char* api_key_env = std::getenv("LLM_API_KEY");
    if (!api_key_env) {
        AGENT_LOG_ERROR("Main") << "Environment variable LLM_API_KEY is not set";
        return 1;
    }

    // 定位配置文件目录
    fs::path config_dir;
    const char* env_config_dir = std::getenv("AGENT_CONFIG_DIR");
    if (env_config_dir && *env_config_dir) {
        config_dir = fs::path(env_config_dir);
    } else {
#ifdef _WIN32
        char exe_path[MAX_PATH];
        GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
        config_dir = fs::path(exe_path).parent_path() / "config";
#else
        config_dir = fs::path(__FILE__).parent_path() / "config";
#endif
    }

    // 解析 agent.md 配置
    auto agent_yaml = agent_cli::parse_yaml_file(config_dir / "agent.md");
    auto& kv = agent_yaml.front_matter;

    auto get_config = [&](const std::string& key, const std::string& default_val = "") -> std::string {
        auto it = kv.find(key);
        if (it != kv.end() && !it->second.empty()) {
            return it->second;
        }
        return default_val;
    };

    auto try_parse_double = [](const std::string& s, double default_val) -> double {
        if (s.empty()) return default_val;
        try { return std::stod(s); } catch (...) { return default_val; }
    };
    auto try_parse_int = [](const std::string& s, int default_val) -> int {
        if (s.empty()) return default_val;
        try { return std::stoi(s); } catch (...) { return default_val; }
    };

    // ========== 构建 AgentConfig ==========
    agent::AgentConfig agent_config;

    // 模型配置（内部据此自动创建 LLM Provider）
    agent_config.model_config.model_type = agent_cli::parse_model_type(get_config("model_type", "DeepSeek"));
    agent_config.model_config.model_name = agent_cli::strtou8(get_config("model_name", "deepseek-v4-flash"));
    agent_config.model_config.api_base_url = agent_cli::strtou8(get_config("api_base_url", "https://api.deepseek.com/v1"));
    agent_config.model_config.api_key = agent_cli::strtou8(api_key_env);
    agent_config.model_config.temperature = try_parse_double(get_config("temperature"), 0.7);
    agent_config.model_config.max_tokens = try_parse_int(get_config("max_tokens"), 8192);
    agent_config.model_config.top_p = try_parse_double(get_config("top_p"), 1.0);

    int max_steps = try_parse_int(get_config("max_steps"), 15);
    bool enable_thinking = (get_config("enable_thinking", "true") == "true");
    bool auto_confirm = (get_config("auto_confirm", "false") == "true");
    bool debug = (get_config("debug", "false") == "true");

    std::string mode_str = get_config("agent_mode", "react");

    // ========== 辅助 LLM 配置构建 ==========
    // 为 Critic/Planner/Executor 构建独立的 LlmModelConfig
    // model_name 为空时返回空 config，agent_impl 会自动回退到主 LLM
    auto build_llm_config = [&](const std::string& prefix, const char* env_var_name) -> agent::LlmModelConfig {
        auto mn = get_config(prefix + "model_name", "");
        if (mn.empty()) return {};
        agent::LlmModelConfig cfg;
        cfg.model_type = agent_cli::parse_model_type(get_config(prefix + "model_type", get_config("model_type")));
        cfg.model_name = agent_cli::strtou8(mn);
        cfg.api_base_url = agent_cli::strtou8(get_config(prefix + "api_base_url", get_config("api_base_url")));
        cfg.temperature = try_parse_double(get_config(prefix + "temperature"),
                                           try_parse_double(get_config("temperature"), 0.7));
        cfg.max_tokens = try_parse_int(get_config(prefix + "max_tokens"),
                                       try_parse_int(get_config("max_tokens"), 8192));
        cfg.top_p = try_parse_double(get_config(prefix + "top_p"),
                                     try_parse_double(get_config("top_p"), 1.0));
        // API key: 先取角色专用环境变量，没有则回退到 LLM_API_KEY
        const char* key = std::getenv(env_var_name);
        if (!key || !*key) key = api_key_env;
        cfg.api_key = agent_cli::strtou8(key);
        return cfg;
    };

    // 命令行参数覆盖
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
                ++i;
            } else if (arg == L"--debug") {
                debug = true;
            }
        }
        LocalFree(argv);
    }
#endif

    // 设置日志级别
    if (debug) {
        agent::log::set_level(agent::log::Level::Debug);
    }

    // Tool Registry（需要注册自定义工具，所以手动设置）
    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<agent_cli::EchoTool>());
    tool_registry->register_tool(std::make_shared<agent_cli::QrCodeTool>());
    tool_registry->register_tool(agent::create_execute_command_tool());
    tool_registry->register_tool(agent::create_python_script_tool());
    tool_registry->register_tool(agent::create_shell_script_tool());
    for (const auto& tool : agent::create_file_system_tools()) {
        tool_registry->register_tool(tool);
    }
    tool_registry->register_tool(agent::create_web_fetch_tool());

    // 搜索工具
    const char* bing_key = std::getenv("BING_SEARCH_KEY");
    if (bing_key && *bing_key) {
        tool_registry->register_tool(agent::create_bing_search_tool(bing_key));
    }
    const char* engines_str = std::getenv("OPENSERP_SEARCH_ENGINES");
    if (engines_str && *engines_str) {
        std::vector<std::string> search_engines;
        std::string s(engines_str);
        std::stringstream ss(s);
        std::string engine;
        while (std::getline(ss, engine, ',')) {
            engine.erase(0, engine.find_first_not_of(" \t"));
            engine.erase(engine.find_last_not_of(" \t") + 1);
            if (!engine.empty()) search_engines.push_back(engine);
        }
        if (!search_engines.empty()) {
            tool_registry->register_tool(agent::create_openserp_search_tool(search_engines));
            AGENT_LOG_INFO("Main") << "OpenSERP configured with engines: " << engines_str;
        }
    }
    const char* bocha_key = std::getenv("BOCHA_SEARCH_KEY");
    if (bocha_key && *bocha_key) {
        tool_registry->register_tool(agent::create_bocha_search_tool(bocha_key));
    }
    const char* volcano_key = std::getenv("VOLCANO_SEARCH_KEY");
    if (volcano_key && *volcano_key) {
        tool_registry->register_tool(agent::create_volcano_search_tool(volcano_key));
    }
    const char* baidu_ai_key = std::getenv("BAIDU_AI_SEARCH_KEY");
    if (baidu_ai_key && *baidu_ai_key) {
        tool_registry->register_tool(agent::create_baidu_ai_search_tool(baidu_ai_key));
        AGENT_LOG_INFO("Main") << "Baidu AI Search enabled";
    }

    agent_config.tool_registry = tool_registry;

    // MCP 配置文件（Agent 内部自动加载并连接 MCP 服务器）
    fs::path mcp_json_path = config_dir / "mcps" / "mcp.json";
    if (fs::exists(mcp_json_path)) {
        agent_config.mcp_config_path = mcp_json_path;
    }

    // Memory
    agent_config.memory = std::make_shared<agent_cli::SimpleMemory>();

    // Personality
    agent::PersonalityDocs& personality = agent_config.personality;
    personality.soul = agent_cli::strtou8(agent_cli::read_file(config_dir / "SOUL.md"));
    personality.identity = agent_cli::strtou8(agent_cli::read_file(config_dir / "IDENTITY.md"));
    personality.agents = agent_cli::strtou8(agent_cli::read_file(config_dir / "AGENTS.md"));

    // Skill 目录（Agent 内部自动扫描并注册 ReadSkillTool）
    fs::path skills_dir = config_dir / "skills";
    if (fs::exists(skills_dir)) {
        agent_config.skill_dirs.push_back(skills_dir);
    }

    // Loop 配置
    agent::InnerLoopConfig loop_config;
    loop_config.max_steps = max_steps;
    loop_config.enable_thinking = enable_thinking;
    loop_config.auto_confirm = auto_confirm;
    loop_config.debug = debug;

    // ========== 创建 Agent ==========
    agent::AgentPtr agent_ptr;

    if (mode_str == "plan_execute") {
        int max_replan_attempts = try_parse_int(get_config("max_replan_attempts"), 3);
        int max_step_retries = try_parse_int(get_config("max_step_retries"), 2);
        agent::LlmModelConfig planner_llm_config = build_llm_config("planner_", "LLM_PLANNER_API_KEY");
        agent::LlmModelConfig executor_llm_config = build_llm_config("executor_", "LLM_EXECUTOR_API_KEY");

        agent::PlanExecuteAgentConfig pe_config;
        static_cast<agent::AgentConfig&>(pe_config) = agent_config;
        pe_config.max_steps = max_steps;
        pe_config.enable_thinking = enable_thinking;
        pe_config.auto_confirm = auto_confirm;
        pe_config.debug = debug;
        pe_config.planner_model_config = planner_llm_config;
        pe_config.executor_model_config = executor_llm_config;
        pe_config.max_step_retries = max_step_retries;
        pe_config.replan_config.max_replan_attempts = max_replan_attempts;
        agent_ptr = agent::Agent::create_plan_execute(pe_config);
    } else if (mode_str == "reflection") {
        int max_reflection_rounds = try_parse_int(get_config("max_reflection_rounds"), 3);
        agent::LlmModelConfig critic_llm_config = build_llm_config("critic_", "LLM_CRITIC_API_KEY");

        agent::ReflectionAgentConfig ref_config;
        static_cast<agent::AgentConfig&>(ref_config) = agent_config;
        ref_config.max_steps = max_steps;
        ref_config.enable_thinking = enable_thinking;
        ref_config.auto_confirm = auto_confirm;
        ref_config.debug = debug;
        ref_config.max_reflection_rounds = max_reflection_rounds;
        ref_config.critic_model_config = critic_llm_config;
        agent_ptr = agent::Agent::create_reflection(ref_config);
    } else {
        agent::ReactAgentConfig react_config;
        static_cast<agent::AgentConfig&>(react_config) = agent_config;
        react_config.max_steps = max_steps;
        react_config.enable_thinking = enable_thinking;
        react_config.auto_confirm = auto_confirm;
        react_config.debug = debug;
        agent_ptr = agent::Agent::create_react(react_config);
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
    agent_ptr->start();

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

    agent_ptr->stop();
    std::cout << "Agent stopped." << std::endl;

    return 0;
}
