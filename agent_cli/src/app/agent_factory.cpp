// agent_cli/src/app/agent_factory.cpp
// Agent 工厂实现：从 agent.md 配置创建 Agent

#include "agent_factory.h"
#include "auto_confirm_handler.h"
#include "utils/utils.h"
#include "web_search/web_search_impl.h"
#include "tools/echo_tool.h"
#include "tools/qr_code_tool.h"
#include "memory/simple_memory.h"
#include <agent/builtin_tools.h>
#include <util/log.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <cstdlib>

namespace agent_cli {

AgentCreateResult create_agent(const fs::path& config_dir,
                               const char* api_key_env,
                               const std::string& mode_override,
                               bool debug_override,
                               bool channel_mode) {
    AgentCreateResult result;

    // 解析 agent.md 配置
    auto agent_yaml = parse_yaml_file(config_dir / "agent.md");
    auto& kv = agent_yaml.front_matter;

    auto get_config = [&](const std::string& key, const std::string& default_val = "") -> std::string {
        auto it = kv.find(key);
        if (it != kv.end() && !it->second.empty()) return it->second;
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
    agent_config.model_config.model_type = parse_model_type(get_config("model_type", "DeepSeek"));
    agent_config.model_config.model_name = strtou8(get_config("model_name", "deepseek-v4-flash"));
    agent_config.model_config.api_base_url = strtou8(get_config("api_base_url", "https://api.deepseek.com/v1"));
    agent_config.model_config.api_key = strtou8(api_key_env);
    agent_config.model_config.temperature = try_parse_double(get_config("temperature"), 0.7);
    agent_config.model_config.max_tokens = try_parse_int(get_config("max_tokens"), 8192);
    agent_config.model_config.top_p = try_parse_double(get_config("top_p"), 1.0);

    int max_steps = try_parse_int(get_config("max_steps"), 15);
    bool enable_thinking = (get_config("enable_thinking", "true") == "true");
    bool auto_confirm = (get_config("auto_confirm", "false") == "true");
    bool debug = debug_override || (get_config("debug", "false") == "true");
    std::string mode_str = mode_override.empty() ? get_config("agent_mode", "react") : mode_override;

    // ========== 辅助 LLM 配置构建 ==========
    auto build_llm_config = [&](const std::string& prefix, const char* env_var_name) -> agent::LlmModelConfig {
        auto mn = get_config(prefix + "model_name", "");
        if (mn.empty()) return {};
        agent::LlmModelConfig cfg;
        cfg.model_type = parse_model_type(get_config(prefix + "model_type", get_config("model_type")));
        cfg.model_name = strtou8(mn);
        cfg.api_base_url = strtou8(get_config(prefix + "api_base_url", get_config("api_base_url")));
        cfg.temperature = try_parse_double(get_config(prefix + "temperature"),
                                           try_parse_double(get_config("temperature"), 0.7));
        cfg.max_tokens = try_parse_int(get_config(prefix + "max_tokens"),
                                       try_parse_int(get_config("max_tokens"), 8192));
        cfg.top_p = try_parse_double(get_config(prefix + "top_p"),
                                     try_parse_double(get_config("top_p"), 1.0));
        const char* key = std::getenv(env_var_name);
        if (!key || !*key) key = api_key_env;
        cfg.api_key = strtou8(key);
        return cfg;
    };

    // ========== 工具注册 ==========
    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<EchoTool>());
    tool_registry->register_tool(std::make_shared<QrCodeTool>());
    tool_registry->register_tool(agent::create_execute_command_tool());
    tool_registry->register_tool(agent::create_python_script_tool());
    tool_registry->register_tool(agent::create_shell_script_tool());
    for (const auto& tool : agent::create_file_system_tools()) {
        tool_registry->register_tool(tool);
    }
    tool_registry->register_tool(agent::create_web_fetch_tool());

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
    }
    agent_config.tool_registry = tool_registry;

    // ========== MCP ==========
    fs::path mcp_json_path = config_dir / "mcps" / "mcp.json";
    if (fs::exists(mcp_json_path)) {
        agent_config.mcp_config_path = mcp_json_path;
    }

    // ========== Memory ==========
    agent_config.memory = std::make_shared<SimpleMemory>();

    // ========== Confirm Handler ==========
    // channel 模式没有 stdin 交互，必须使用自动确认，否则需要确认的工具会永久阻塞
    if (channel_mode) {
        agent_config.confirm_handler = std::make_shared<AutoConfirmHandler>();
    }

    // ========== Personality ==========
    agent::PersonalityDocs& personality = agent_config.personality;
    personality.soul = strtou8(read_file(config_dir / "SOUL.md"));
    personality.identity = strtou8(read_file(config_dir / "IDENTITY.md"));
    personality.agents = strtou8(read_file(config_dir / "AGENTS.md"));

    // ========== Skill 目录 ==========
    fs::path skills_dir = config_dir / "skills";
    if (fs::exists(skills_dir)) {
        agent_config.skill_dirs.push_back(skills_dir);
    }

    // ========== 创建 Agent ==========
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
        result.agent = agent::Agent::create_plan_execute(pe_config);
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
        result.agent = agent::Agent::create_reflection(ref_config);
    } else {
        agent::ReactAgentConfig react_config;
        static_cast<agent::AgentConfig&>(react_config) = agent_config;
        react_config.max_steps = max_steps;
        react_config.enable_thinking = enable_thinking;
        react_config.auto_confirm = auto_confirm;
        react_config.debug = debug;
        result.agent = agent::Agent::create_react(react_config);
    }

    result.mode_str = mode_str;
    result.debug = debug;
    return result;
}

} // namespace agent_cli
