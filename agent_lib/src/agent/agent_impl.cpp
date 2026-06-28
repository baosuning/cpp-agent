#include "agent_impl.h"
#include <agent/agent.h>
#include <util/u8str_utils.h>
#include <util/log.h>
#include "agent/prompt_builder.h"
#include "agent/context_manager.h"
#include "../confirm/default_confirm_handler.h"
#include "../tool/tool_registry.h"
#include "../mcp/mcp_manager.h"
#include "../mcp/mcp_client.h"
#include "../llm/llm_provider_factory.h"
#include "../skill/skill_scanner.h"
#include "../tool/read_skill_tool.h"
#include "../tool/load_mcp_tool.h"
#include "../memory/default_memory.h"
#include "react_loop.h"
#include "plan_execute_loop.h"
#include "reflection_loop.h"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace agent {

constexpr int kMaxAutoContinue = 20;

// ========== 构造函数 ==========

// ========== ReAct 构造函数 ==========
Agent::Impl::Impl(const ReactAgentConfig& config)
{
    init_components(config);

    agent_loop_factory_ = [config](const AgentLoopContext& ctx)
        -> std::shared_ptr<IAgentLoop> {
        return std::make_shared<ReactLoop>(
            ctx.llm_provider, ctx.confirm_handler,
            ctx.context_manager, ctx.prompt_builder,
            ctx.tools, ctx.mcps, ctx.memory,
            ctx.personality, ReactLoopConfig{config}, ctx.token_accumulator
        );
    };
}

// ========== PE 构造函数 ==========
Agent::Impl::Impl(const PlanExecuteAgentConfig& config)
{
    init_components(config);

    // PE 特有：创建 planner LLM
    if (config.planner_llm_provider) {
        planner_llm_ = config.planner_llm_provider;
    } else if (!config.planner_model_config.api_key.empty()) {
        planner_llm_ = LlmProviderFactory::create(config.planner_model_config);
        if (!planner_llm_) planner_llm_ = llm_provider_; // fallback for unsupported types
    } else {
        planner_llm_ = llm_provider_;
    }

    // PE 特有：创建 executor LLM
    if (config.executor_llm_provider) {
        executor_llm_ = config.executor_llm_provider;
    } else if (!config.executor_model_config.api_key.empty()) {
        executor_llm_ = LlmProviderFactory::create(config.executor_model_config);
        if (!executor_llm_) executor_llm_ = llm_provider_; // fallback for unsupported types
    } else {
        executor_llm_ = llm_provider_;
    }

    // PE 工厂：闭包捕获 planner/executor LLM
    agent_loop_factory_ = [config, planner = planner_llm_, executor = executor_llm_]
        (const AgentLoopContext& ctx) -> std::shared_ptr<IAgentLoop> {
        InnerLoopConfig loop_cfg;
        loop_cfg.max_steps = config.max_steps;
        loop_cfg.enable_thinking = config.enable_thinking;
        loop_cfg.auto_confirm = config.auto_confirm;
        loop_cfg.debug = config.debug;
        return std::make_shared<PlanExecuteLoop>(
            ctx.llm_provider, ctx.confirm_handler,
            ctx.context_manager, ctx.prompt_builder,
            ctx.tools, ctx.mcps, ctx.memory,
            ctx.personality, loop_cfg,
            planner, executor, ctx.token_accumulator
        );
    };
}

// ========== Reflection 构造函数 ==========
Agent::Impl::Impl(const ReflectionAgentConfig& config)
{
    init_components(config);

    // Reflection 特有：创建 Critic LLM
    if (config.critic_llm_provider) {
        critic_llm_ = config.critic_llm_provider;
    } else if (!config.critic_model_config.api_key.empty()) {
        critic_llm_ = LlmProviderFactory::create(config.critic_model_config);
        if (!critic_llm_) critic_llm_ = llm_provider_; // fallback for unsupported types
    } else {
        critic_llm_ = llm_provider_; // 单模型模式
    }

    // Reflection 工厂：闭包捕获 critic LLM
    agent_loop_factory_ = [config, critic = critic_llm_]
        (const AgentLoopContext& ctx) -> std::shared_ptr<IAgentLoop> {
        InnerLoopConfig loop_cfg;
        loop_cfg.max_steps = config.max_steps;
        loop_cfg.enable_thinking = config.enable_thinking;
        loop_cfg.auto_confirm = config.auto_confirm;
        loop_cfg.debug = config.debug;
        return std::make_shared<ReflectionLoop>(
            ctx.llm_provider, ctx.confirm_handler,
            ctx.context_manager, ctx.prompt_builder,
            ctx.tools, ctx.mcps, ctx.memory,
            ctx.personality, ReflectionLoopConfig{config},
            critic, ctx.token_accumulator
        );
    };
}

// ========== 自定义 Loop 构造函数 ==========
Agent::Impl::Impl(AgentLoopFactory loop_factory, const AgentConfig& config)
{
    init_components(config);
    agent_loop_factory_ = std::move(loop_factory);
}

Agent::Impl::~Impl()
{
    stop();
}

// ========== 组件初始化 ==========

void Agent::Impl::init_components(const AgentConfig& config)
{
    // LLM Provider：外部提供则用外部的，否则根据 model_type + model_config 自动创建
    if (config.llm_provider) {
        llm_provider_ = config.llm_provider;
    } else {
        llm_provider_ = LlmProviderFactory::create(config.model_config);
        if (!llm_provider_) {
            AGENT_LOG_ERROR("Agent") << "Failed to create LLM provider for model type "
                          << static_cast<int>(config.model_config.model_type);
        }
    }

    // 其他组件：nullptr 则使用内置默认实现
    confirm_handler_ = config.confirm_handler
        ? config.confirm_handler
        : std::make_shared<DefaultConfirmHandler>();

    prompt_builder_ = config.prompt_builder
        ? config.prompt_builder
        : std::make_shared<PromptBuilder>();

    context_ = config.context_manager
        ? config.context_manager
        : std::make_shared<DefaultContextManager>();

    tools_ = config.tool_registry
        ? config.tool_registry
        : std::make_shared<ToolRegistry>();

    mcps_ = std::make_shared<McpManager>();

    memory_ = config.memory
        ? config.memory
        : std::make_shared<DefaultMemory>();

    personality_docs_ = config.personality;

    // MCP 配置自动加载：读取 mcp_config_path 指向的 JSON 文件，连接 MCP 服务器
    if (!config.mcp_config_path.empty() && std::filesystem::exists(config.mcp_config_path)) {
        try {
            std::ifstream ifs(config.mcp_config_path);
            std::string content((std::istreambuf_iterator<char>(ifs)),
                                 std::istreambuf_iterator<char>());
            auto mcp_config = nlohmann::json::parse(content);
            if (mcp_config.contains("mcpServers") && mcp_config["mcpServers"].is_object()) {
                for (auto& [name, server_config] : mcp_config["mcpServers"].items()) {
                    try {
                        auto mcp_cfg = mcp::McpClientConfig::from_json(server_config, name);
                        // 配置中存在未设置的环境变量占位符时静默跳过，不输出任何日志
                        if (mcp_cfg.skip) {
                            continue;
                        }
                        auto mcp_client = std::make_shared<mcp::McpClient>(mcp_cfg);
                        if (mcp_client->connect()) {
                            mcps_->register_mcp(mcp_client);
                            AGENT_LOG_INFO("Agent") << "MCP '" << name << "' connected";
                        } else {
                            AGENT_LOG_WARN("Agent") << "MCP '" << name << "' connection failed";
                        }
                    } catch (const std::exception& e) {
                        AGENT_LOG_ERROR("Agent") << "MCP '" << name << "' error: " << e.what();
                    }
                }
            }
        } catch (const std::exception& e) {
            AGENT_LOG_ERROR("Agent") << "Failed to parse mcp config: " << e.what();
        }
    }

    // 注册 load_mcp_tool：LLM 通过此工具按需加载 MCP 工具完整 schema
    // 当有 MCP 服务器连接时始终注册，即使没有 MCP 工具也可注册（不会造成问题）
    if (!mcps_->list_mcps().empty()) {
        tools_->register_tool(std::make_shared<LoadMcpToolTool>(*mcps_));
        AGENT_LOG_INFO("Agent") << "Registered load_mcp_tool";
    }

    // Skill 目录自动扫描：扫描 skill_dirs 中的目录，注册 ReadSkillTool，更新 personality
    if (!config.skill_dirs.empty()) {
        auto skill_scanner = std::make_shared<SkillScanner>();
        for (const auto& dir : config.skill_dirs) {
            if (std::filesystem::exists(dir)) {
                skill_scanner->scan_skills(dir);
            }
        }
        if (!skill_scanner->list_skills().empty()) {
            tools_->register_tool(std::make_shared<ReadSkillTool>(skill_scanner));

            // 将 skill 信息写入 personality.skill_doc
            auto skills = skill_scanner->list_skills();
            std::string skill_doc;
            // 在技能列表前加上 skills 根目录路径，方便 LLM 构造脚本调用命令
            if (!config.skill_dirs.empty()) {
                skill_doc += "Skills directory: " +
                             std::filesystem::absolute(config.skill_dirs[0]).string() + "\n\n";
            }
            for (auto& skill : skills) {
                skill_doc += "- **" + skill.name + "**: " + skill.description + "\n";
            }
            personality_docs_.skill_doc = u8str_util::to_u8str(skill_doc);
        }
    }
}

// ========== 生命周期 ==========

void Agent::Impl::start()
{
    if (!llm_provider_) {
        AGENT_LOG_ERROR("Agent") << "LLM not configured";
        return;
    }

    if (running_.exchange(true)) {
        return;
    }
    worker_thread_ = std::thread(&Impl::process_loop, this);
}

void Agent::Impl::stop()
{
    if (!running_.exchange(false)) {
        return;
    }

    queue_cv_.notify_all();
    auto loop = current_loop_;
    if (loop) {
        loop->stop();
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    // Agent 停止时清理浏览器会话
    cleanup_browser_sessions();
}

void Agent::Impl::submit_input(const u8str& input)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        input_queue_.push(input);
    }
    queue_cv_.notify_one();
}

void Agent::Impl::process_loop()
{
    bool last_was_auto = false;
    int  auto_continue_count = 0;

    while (running_.load()) {
        u8str input;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !input_queue_.empty() || !running_.load(); });
            if (!running_.load()) break;
            if (input_queue_.empty()) continue;
            input = input_queue_.front();
            input_queue_.pop();
        }

        if (!last_was_auto) {
            auto_continue_count = 0;
        }

        // 使用 agent_loop_factory_ 创建 Loop
        AgentLoopContext ctx{
            *context_, *prompt_builder_, *tools_, *mcps_, *memory_, personality_docs_,
            llm_provider_, confirm_handler_, &token_accumulator_
        };

        try {
            current_loop_ = agent_loop_factory_(ctx);
        } catch (const std::exception& e) {
            AGENT_LOG_ERROR("Agent") << "agent_loop_factory threw: " << e.what();
            // 必须触发 Error 状态，否则等待 Idle 的调用方（如 Channel）会永久阻塞
            if (on_state_change_) on_state_change_(AgentState::Error);
            std::string err = "[Error] agent_loop_factory threw: " + std::string(e.what());
            if (on_output_ready_) on_output_ready_(u8str(err.begin(), err.end()));
            continue;
        }

        if (!current_loop_) {
            AGENT_LOG_ERROR("Agent") << "agent_loop_factory returned nullptr";
            if (on_state_change_) on_state_change_(AgentState::Error);
            if (on_output_ready_) on_output_ready_(u8str(u8"[Error] agent_loop_factory returned nullptr"));
            continue;
        }

        // 取局部引用，后续操作通过 loop 访问，避免 current_loop_ 被并发重置
        auto loop = current_loop_;

        // 绑定回调
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (on_thinking_update_) {
                loop->set_on_thinking_update(on_thinking_update_);
            }
            if (on_output_ready_) {
                loop->set_on_output_ready(on_output_ready_);
            }
            if (on_state_change_) {
                loop->set_on_state_change(on_state_change_);
            }
        }

        // 执行 Loop
        try {
            loop->run(input);
        } catch (const std::exception& e) {
            std::string err = "[Fatal Error] " + std::string(e.what());
            u8str error_msg(err.begin(), err.end());
            AGENT_LOG_ERROR("Agent") << "Fatal error in run(): " << err;
            if (on_state_change_) on_state_change_(AgentState::Error);
            if (on_output_ready_) on_output_ready_(error_msg);
        }

        // 上下文压缩（异常不能让 worker 线程死亡，否则后续 submit_input 永远不会被处理）
        try {
            context_->compress();
        } catch (const std::exception& e) {
            AGENT_LOG_ERROR("Agent") << "context compress failed: " << e.what();
        }

        // 自动继续逻辑
        last_was_auto = false;
        if (running_.load() && auto_continue_count < kMaxAutoContinue && !u8str_util::is_stop_intent(input)) {
            auto output = loop->get_final_output();
            auto state = loop->get_state();

            if (state == AgentState::Error || state == AgentState::WaitingUserConfirm) {
                // 不自动继续
            } else if (output && loop->should_auto_continue()) {
                auto_continue_count++;
                last_was_auto = true;
                AGENT_LOG_DEBUG("Agent") << "Auto-continue #" << auto_continue_count;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    input_queue_.push(u8str(u8"继续"));
                }
                queue_cv_.notify_one();
            }
        }

        // 只有在不自动继续时，才通知 Idle 状态（表示真正完成，等待用户输入）
        if (!last_was_auto) {
            // 不在此处清理浏览器，浏览器应保持打开直到 Agent 完全停止
            if (on_state_change_) on_state_change_(AgentState::Idle);
        }
    }

    // Agent 停止时也通知 Idle，确保主线程能退出等待
    if (on_state_change_) on_state_change_(AgentState::Idle);
}

// ========== 状态查询 ==========

std::vector<ThinkingStep> Agent::Impl::get_thinking() const {
    auto loop = current_loop_;
    if (loop) {
        return loop->get_thinking_steps();
    }
    return {};
}

std::optional<u8str> Agent::Impl::get_output() const {
    auto loop = current_loop_;
    if (loop) {
        return loop->get_final_output();
    }
    return std::nullopt;
}

ContextSnapshot Agent::Impl::get_context() const {
    AgentState state = AgentState::Idle;
    std::vector<ThinkingStep> thinking;
    std::optional<u8str> output;
    auto loop = current_loop_;
    if (loop) {
        state = loop->get_state();
        thinking = loop->get_thinking_steps();
        output = loop->get_final_output();
    }
    if (context_) {
        return context_->get_snapshot(state, thinking, output);
    }
    ContextSnapshot snapshot;
    snapshot.state = state;
    snapshot.thinking_steps = thinking;
    snapshot.final_output = output;
    return snapshot;
}

AgentState Agent::Impl::get_state() const {
    auto loop = current_loop_;
    if (loop) {
        return loop->get_state();
    }
    return AgentState::Idle;
}

std::optional<Plan> Agent::Impl::get_plan() const {
    auto loop = current_loop_;
    if (loop) {
        return loop->get_plan();
    }
    return std::nullopt;
}

std::optional<PlanExecutionLog> Agent::Impl::get_execution_log() const {
    auto loop = current_loop_;
    if (loop) {
        return loop->get_execution_log();
    }
    return std::nullopt;
}

TokenUsageStats Agent::Impl::get_token_stats() const {
    return token_accumulator_.snapshot();
}

void Agent::Impl::reset_token_stats() {
    token_accumulator_.reset();
}

// ========== 组件查询 ==========

LlmProviderPtr Agent::Impl::get_llm_provider() const { return llm_provider_; }
UserConfirmHandlerPtr Agent::Impl::get_confirm_handler() const { return confirm_handler_; }
PromptBuilderPtr Agent::Impl::get_prompt_builder() const { return prompt_builder_; }
ContextManagerPtr Agent::Impl::get_context_manager() const { return context_; }
ToolRegistryPtr Agent::Impl::get_tool_registry() const { return tools_; }
McpManagerPtr Agent::Impl::get_mcp_manager() const { return mcps_; }
MemoryPtr Agent::Impl::get_memory() const { return memory_; }
const PersonalityDocs& Agent::Impl::get_personality() const { return personality_docs_; }

// ========== 回调 ==========

void Agent::Impl::set_on_thinking_update(std::function<void(const ThinkingStep&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_thinking_update_ = std::move(callback);
}

void Agent::Impl::set_on_output_ready(std::function<void(const u8str&)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_output_ready_ = std::move(callback);
}

void Agent::Impl::set_on_state_change(std::function<void(AgentState)> callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    on_state_change_ = std::move(callback);
}

// ========== 浏览器会话清理 ==========

void Agent::Impl::cleanup_browser_sessions() {
    if (!mcps_) return;
    try {
        auto mcp_list = mcps_->list_mcps();
        for (const auto& mcp : mcp_list) {
            try {
                auto tools = mcp->list_tools();
                for (const auto& tool : tools) {
                    if (!tool.contains("name")) continue;
                    std::string tool_name = tool["name"].get<std::string>();
                    // 检查是否为浏览器关闭工具（case-insensitive）
                    auto icontains = [](const std::string& s, const std::string& sub) {
                        return std::search(s.begin(), s.end(), sub.begin(), sub.end(),
                            [](unsigned char a, unsigned char b) {
                                return std::tolower(a) == std::tolower(b);
                            }) != s.end();
                    };
                    bool is_browser_close =
                        (icontains(tool_name, "browser") && icontains(tool_name, "close")) ||
                        (icontains(tool_name, "cloak") && icontains(tool_name, "close")) ||
                        (icontains(tool_name, "close_browser"));
                    if (is_browser_close) {
                        u8str method_u8(tool_name.begin(), tool_name.end());
                        mcp->call(method_u8, u8str(u8"{}"));
                        AGENT_LOG_DEBUG("Agent") << "Browser cleanup: called " << tool_name;
                    }
                }
            } catch (const std::exception& e) {
                AGENT_LOG_DEBUG("Agent") << "Browser cleanup skipped for MCP: " << e.what();
            }
        }
    } catch (...) {
        AGENT_LOG_DEBUG("Agent") << "Browser cleanup failed silently (non-critical)";
    }
}

// ========== Agent 转发层 ==========

Agent::~Agent() = default;

AgentPtr Agent::create_react(const ReactAgentConfig& config) {
    auto agent = AgentPtr(new Agent());
    agent->pimpl_ = std::make_unique<Impl>(config);
    return agent;
}

AgentPtr Agent::create_plan_execute(const PlanExecuteAgentConfig& config) {
    auto agent = AgentPtr(new Agent());
    agent->pimpl_ = std::make_unique<Impl>(config);
    return agent;
}

AgentPtr Agent::create_reflection(const ReflectionAgentConfig& config) {
    auto agent = AgentPtr(new Agent());
    agent->pimpl_ = std::make_unique<Impl>(config);
    return agent;
}

AgentPtr Agent::create_custom(AgentLoopFactory loop_factory, const AgentConfig& config) {
    auto agent = AgentPtr(new Agent());
    agent->pimpl_ = std::make_unique<Impl>(std::move(loop_factory), config);
    return agent;
}

void Agent::start() { pimpl_->start(); }
void Agent::stop() { pimpl_->stop(); }
void Agent::submit_input(const u8str& input) { pimpl_->submit_input(input); }

std::vector<ThinkingStep> Agent::get_thinking() const { return pimpl_->get_thinking(); }
std::optional<u8str> Agent::get_output() const { return pimpl_->get_output(); }
ContextSnapshot Agent::get_context() const { return pimpl_->get_context(); }
AgentState Agent::get_state() const { return pimpl_->get_state(); }

std::optional<Plan> Agent::get_plan() const { return pimpl_->get_plan(); }
std::optional<PlanExecutionLog> Agent::get_execution_log() const { return pimpl_->get_execution_log(); }
TokenUsageStats Agent::get_token_stats() const { return pimpl_->get_token_stats(); }
void Agent::reset_token_stats() { pimpl_->reset_token_stats(); }

LlmProviderPtr Agent::get_llm_provider() const { return pimpl_->get_llm_provider(); }
UserConfirmHandlerPtr Agent::get_confirm_handler() const { return pimpl_->get_confirm_handler(); }
PromptBuilderPtr Agent::get_prompt_builder() const { return pimpl_->get_prompt_builder(); }
ContextManagerPtr Agent::get_context_manager() const { return pimpl_->get_context_manager(); }
ToolRegistryPtr Agent::get_tool_registry() const { return pimpl_->get_tool_registry(); }
McpManagerPtr Agent::get_mcp_manager() const { return pimpl_->get_mcp_manager(); }
MemoryPtr Agent::get_memory() const { return pimpl_->get_memory(); }
const PersonalityDocs& Agent::get_personality() const { return pimpl_->get_personality(); }

void Agent::set_on_thinking_update(std::function<void(const ThinkingStep&)> callback) { pimpl_->set_on_thinking_update(std::move(callback)); }
void Agent::set_on_output_ready(std::function<void(const u8str&)> callback) { pimpl_->set_on_output_ready(std::move(callback)); }
void Agent::set_on_state_change(std::function<void(AgentState)> callback) { pimpl_->set_on_state_change(std::move(callback)); }

} // namespace agent
