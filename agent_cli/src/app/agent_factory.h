#pragma once
// agent_cli/src/app/agent_factory.h
// Agent 工厂：从配置文件创建 Agent（含工具/MCP/记忆/人格）
// 供 CLI 模式和 WeChat 模式共享

#include <agent/agent.h>
#include <filesystem>
#include <string>

namespace agent_cli {

namespace fs = std::filesystem;

// Agent 创建结果
struct AgentCreateResult {
    agent::AgentPtr agent;       // 创建好的 Agent（已注册工具/MCP/记忆/人格）
    std::string     mode_str;    // 实际使用的模式（命令行覆盖后的值）
    bool            debug = false;
};

// 从 config_dir/agent.md 创建 Agent
// api_key_env: LLM_API_KEY 环境变量值
// mode_override: 非空时覆盖配置文件的 agent_mode
// debug_override: true 时强制开启 debug
// channel_mode: true 表示运行在 channel 模式（无 stdin 交互），使用自动确认处理器
AgentCreateResult create_agent(const fs::path& config_dir,
                               const char* api_key_env,
                               const std::string& mode_override = "",
                               bool debug_override = false,
                               bool channel_mode = false);

} // namespace agent_cli
