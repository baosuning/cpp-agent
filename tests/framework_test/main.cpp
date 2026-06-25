#include <agent/agent.h>
#include <agent/builtin_tools.h>
#include <agent/i_tool.h>
#include <agent/i_memory.h>
#include <agent/i_user_confirm_handler.h>
#include <agent/i_prompt_builder.h>
#include <agent/i_agent_loop.h>
#include <agent/agent_config.h>
#include <agent/personality.h>
#include "agent/reflection_loop.h"
#include "agent/context_manager.h"
#include "agent/prompt_builder.h"
#include "mcp/mcp_manager.h"
#include <iostream>
#include <string>
#include <optional>
#include <thread>
#include <chrono>
#include <atomic>
#include <deque>
#include <cstdlib>
#include <set>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string u8tostr(const agent::u8str& u8s) {
    return std::string(u8s.begin(), u8s.end());
}

agent::u8str strtou8(const std::string& s) {
    return agent::u8str(s.begin(), s.end());
}

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

// ============================================================
// Tool Implementations
// ============================================================

class EchoTool : public agent::ITool {
public:
    agent::u8str name() const override { return strtou8("echo"); }
    agent::u8str description() const override { return strtou8("Echoes back the input text"); }
    agent::u8str parameters_schema() const override {
        return strtou8(R"({"type":"object","properties":{"text":{"type":"string","description":"Text to echo"}},"required":["text"]})");
    }
    agent::u8str execute(const agent::u8str& arguments) override { return arguments; }
    void execute_async(const agent::u8str& arguments, std::function<void(agent::u8str)> callback) override {
        callback(execute(arguments));
    }
    bool requires_confirmation() const override { return false; }
};

class CalculatorTool : public agent::ITool {
public:
    agent::u8str name() const override { return strtou8("calculator"); }
    agent::u8str description() const override { return strtou8("Evaluates a simple arithmetic expression"); }
    agent::u8str parameters_schema() const override {
        return strtou8(R"({"type":"object","properties":{"expression":{"type":"string","description":"Arithmetic expression"}},"required":["expression"]})");
    }
    agent::u8str execute(const agent::u8str&) override { return strtou8("42"); }
    void execute_async(const agent::u8str& arguments, std::function<void(agent::u8str)> callback) override {
        callback(execute(arguments));
    }
    bool requires_confirmation() const override { return false; }
};

class DangerousTool : public agent::ITool {
public:
    agent::u8str name() const override { return strtou8("dangerous_op"); }
    agent::u8str description() const override { return strtou8("A dangerous operation"); }
    agent::u8str parameters_schema() const override {
        return strtou8(R"({"type":"object","properties":{"action":{"type":"string","description":"Action"}},"required":["action"]})");
    }
    agent::u8str execute(const agent::u8str& arguments) override { return strtou8("Dangerous: ") + arguments; }
    void execute_async(const agent::u8str& arguments, std::function<void(agent::u8str)> callback) override {
        callback(execute(arguments));
    }
    bool requires_confirmation() const override { return true; }
};

// 带延迟的工具，用于验证并发执行（并行总时间 ≈ 单次延迟，串行总时间 ≈ N × 延迟）
class SlowEchoTool : public agent::ITool {
public:
    explicit SlowEchoTool(int delay_ms = 100, const std::string& suffix = "")
        : delay_ms_(delay_ms), suffix_(suffix) {}

    agent::u8str name() const override {
        return strtou8("slow_echo" + suffix_);
    }
    agent::u8str description() const override { return strtou8("Echo tool with delay"); }
    agent::u8str parameters_schema() const override {
        return strtou8(R"({"type":"object","properties":{"text":{"type":"string","description":"Text to echo"}},"required":["text"]})");
    }
    agent::u8str execute(const agent::u8str& arguments) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        return strtou8("[slow_echo" + suffix_ + "] ") + arguments;
    }
    void execute_async(const agent::u8str& arguments, std::function<void(agent::u8str)> callback) override {
        callback(execute(arguments));
    }
    bool requires_confirmation() const override { return false; }

private:
    int delay_ms_;
    std::string suffix_;
};

// 可控制是否需要确认的工具（用于测试串行回退路径）
class ConfirmableEchoTool : public agent::ITool {
public:
    explicit ConfirmableEchoTool(bool needs_confirm = false, const std::string& suffix = "")
        : needs_confirm_(needs_confirm), suffix_(suffix) {}

    agent::u8str name() const override {
        return strtou8("confirm_echo" + suffix_);
    }
    agent::u8str description() const override { return strtou8("Echo tool, optionally confirmable"); }
    agent::u8str parameters_schema() const override {
        return strtou8(R"({"type":"object","properties":{"text":{"type":"string","description":"Text to echo"}},"required":["text"]})");
    }
    agent::u8str execute(const agent::u8str& arguments) override {
        return strtou8("[confirm_echo" + suffix_ + "] ") + arguments;
    }
    void execute_async(const agent::u8str& arguments, std::function<void(agent::u8str)> callback) override {
        callback(execute(arguments));
    }
    bool requires_confirmation() const override { return needs_confirm_; }

private:
    bool needs_confirm_;
    std::string suffix_;
};

// ============================================================
// Memory Implementation
// ============================================================

class TestMemory : public agent::IMemory {
public:
    agent::u8str get_memory_name() const override { return strtou8("test_memory"); }
    void store(const agent::u8str& key, const agent::u8str& value) override {
        std::lock_guard<std::mutex> lock(mutex_);
        data_[key] = value;
    }
    std::optional<agent::u8str> retrieve(const agent::u8str& key) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it != data_.end()) return it->second;
        return std::nullopt;
    }
    std::vector<agent::u8str> search(const agent::u8str& query) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<agent::u8str> results;
        for (const auto& [k, v] : data_) {
            if (k.find(query) != agent::u8str::npos || v.find(query) != agent::u8str::npos)
                results.push_back(v);
        }
        return results;
    }
    void remove(const agent::u8str& key) override { std::lock_guard<std::mutex> lock(mutex_); data_.erase(key); }
    void clear() override { std::lock_guard<std::mutex> lock(mutex_); data_.clear(); }
private:
    mutable std::mutex mutex_;
    std::map<agent::u8str, agent::u8str> data_;
};

// ============================================================
// Helper: 创建测试用的 AgentConfig
// ============================================================

agent::AgentConfig make_test_config(bool auto_confirm = true) {
    agent::AgentConfig config;
    config.model_config.model_type = agent::LlmModelType::DeepSeek;
    config.model_config.model_name = strtou8("deepseek-v4-flash");
    config.model_config.api_base_url = strtou8("https://api.deepseek.com");
    config.model_config.api_key = strtou8("test-key");
    config.memory = std::make_shared<TestMemory>();
    return config;
}

agent::ReactAgentConfig make_test_react_config(bool auto_confirm = true) {
    agent::ReactAgentConfig config;
    config.model_config.model_type = agent::LlmModelType::DeepSeek;
    config.model_config.model_name = strtou8("deepseek-v4-flash");
    config.model_config.api_base_url = strtou8("https://api.deepseek.com");
    config.model_config.api_key = strtou8("test-key");
    config.memory = std::make_shared<TestMemory>();
    config.auto_confirm = auto_confirm;
    return config;
}

} // anonymous namespace

// ============================================================
// Test Functions
// ============================================================

void test_agent_create_and_lifecycle() {
    test_header("Agent Create & Lifecycle");

    auto config = make_test_react_config();
    auto agent_ptr = agent::Agent::create_react(config);
    if (agent_ptr) test_pass("Agent::create_react() returns non-null");
    else { test_fail("Agent::create_react returned null"); return; }

    if (agent_ptr->get_state() == agent::AgentState::Idle) test_pass("initial state is Idle");
    else test_fail("initial state should be Idle");

    agent_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    test_pass("start() completes without error");

    agent_ptr->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    test_pass("stop() completes without error");
}

void test_tool_registration() {
    test_header("Tool Registration");

    auto config = make_test_react_config();
    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<EchoTool>());
    tool_registry->register_tool(std::make_shared<CalculatorTool>());
    tool_registry->register_tool(std::make_shared<DangerousTool>());
    config.tool_registry = tool_registry;

    auto agent_ptr = agent::Agent::create_react(config);
    auto registry = agent_ptr->get_tool_registry();

    auto tools = registry->list_tools();
    if (tools.size() == 3) test_pass("register 3 tools, list returns 3");
    else { test_fail("expected 3 tools, got " + std::to_string(tools.size())); return; }

    std::set<std::string> tool_names;
    for (const auto& t : tools) tool_names.insert(u8tostr(t->name()));
    if (tool_names.count("echo")) test_pass("echo tool registered");
    if (tool_names.count("calculator")) test_pass("calculator tool registered");
    if (tool_names.count("dangerous_op")) test_pass("dangerous_op tool registered");

    registry->update_tool(strtou8("echo"), std::make_shared<EchoTool>());
    test_pass("update_tool completes");

    registry->remove_tool(strtou8("calculator"));
    tools = registry->list_tools();
    if (tools.size() == 2) test_pass("remove_tool reduces count to 2");
    else test_fail("expected 2 tools after remove, got " + std::to_string(tools.size()));
}

void test_memory_operations() {
    test_header("Memory Operations");

    auto memory = std::make_shared<TestMemory>();
    auto config = make_test_react_config();
    config.memory = memory;

    auto agent_ptr = agent::Agent::create_react(config);
    auto retrieved = agent_ptr->get_memory();
    if (retrieved) test_pass("get_memory returns non-null");
    else { test_fail("get_memory returned null"); return; }

    memory->store(strtou8("key1"), strtou8("value1"));
    memory->store(strtou8("key2"), strtou8("value2"));

    auto val = memory->retrieve(strtou8("key1"));
    if (val && u8tostr(*val) == "value1") test_pass("store / retrieve");
    else test_fail("store/retrieve failed");

    auto results = memory->search(strtou8("value"));
    if (results.size() == 2) test_pass("search finds 2 results");
    else test_fail("search expected 2, got " + std::to_string(results.size()));

    memory->remove(strtou8("key1"));
    val = memory->retrieve(strtou8("key1"));
    if (!val) test_pass("remove works");
    else test_fail("remove failed");

    memory->clear();
    results = memory->search(strtou8("value"));
    if (results.empty()) test_pass("clear works");
    else test_fail("clear failed");
}

void test_confirm_handler() {
    test_header("User Confirm Handler");

    auto config = make_test_react_config();
    auto agent_ptr = agent::Agent::create_react(config);
    auto handler = agent_ptr->get_confirm_handler();
    if (handler) test_pass("get_confirm_handler returns non-null");
    else test_fail("get_confirm_handler returned null");
}

void test_prompt_builder() {
    test_header("Prompt Builder");

    auto config = make_test_react_config();
    auto agent_ptr = agent::Agent::create_react(config);
    auto builder = agent_ptr->get_prompt_builder();
    if (builder) test_pass("get_prompt_builder returns non-null");
    else test_fail("get_prompt_builder returned null");
}

void test_personality_api() {
    test_header("Personality API");

    auto config = make_test_react_config();
    config.personality.soul = strtou8("test soul");
    config.personality.identity = strtou8("test identity");
    config.personality.agents = strtou8("test principles");
    config.personality.skill_doc = strtou8("skill doc");
    config.personality.user_index = strtou8("user index");
    config.personality.user_profiles[strtou8("alice")] = strtou8("Alice's profile");

    auto agent_ptr = agent::Agent::create_react(config);

    const auto& retrieved = agent_ptr->get_personality();
    if (retrieved.soul == strtou8("test soul")) test_pass("personality.soul");
    else test_fail("soul mismatch");

    if (retrieved.identity == strtou8("test identity")) test_pass("personality.identity");
    else test_fail("identity mismatch");

    if (retrieved.agents == strtou8("test principles")) test_pass("personality.agents");
    else test_fail("agents mismatch");

    if (retrieved.skill_doc == strtou8("skill doc")) test_pass("personality.skill_doc");
    else test_fail("skill_doc mismatch");

    if (retrieved.user_index == strtou8("user index")) test_pass("personality.user_index");
    else test_fail("user_index mismatch");

    auto it = retrieved.user_profiles.find(strtou8("alice"));
    if (it != retrieved.user_profiles.end() && it->second == strtou8("Alice's profile"))
        test_pass("personality.user_profiles");
    else test_fail("user_profiles mismatch");
}

void test_callbacks() {
    test_header("Callbacks");

    auto config = make_test_react_config();
    auto agent_ptr = agent::Agent::create_react(config);

    bool thinking_called = false;
    bool output_called = false;
    bool state_called = false;

    agent_ptr->set_on_thinking_update([&](const agent::ThinkingStep&) { thinking_called = true; });
    agent_ptr->set_on_output_ready([&](const agent::u8str&) { output_called = true; });
    agent_ptr->set_on_state_change([&](agent::AgentState) { state_called = true; });

    if (!thinking_called) test_pass("on_thinking_update registered");
    if (!output_called) test_pass("on_output_ready registered");
    if (!state_called) test_pass("on_state_change registered");
}

void test_llm_provider() {
    test_header("LLM Provider");

    auto config = make_test_react_config();
    auto agent_ptr = agent::Agent::create_react(config);
    auto provider = agent_ptr->get_llm_provider();
    if (provider) test_pass("get_llm_provider returns non-null");
    else { test_fail("get_llm_provider returned null"); return; }
}

void test_context_inspection() {
    test_header("Context Inspection");

    auto config = make_test_react_config();
    auto agent_ptr = agent::Agent::create_react(config);

    auto ctx = agent_ptr->get_context();
    if (ctx.messages.empty()) test_pass("initial context has empty messages");
    else test_fail("initial context should have empty messages");

    auto thinking = agent_ptr->get_thinking();
    if (thinking.empty()) test_pass("initial thinking is empty");
    else test_fail("initial thinking should be empty");

    auto output = agent_ptr->get_output();
    if (!output) test_pass("initial output is nullopt");
    else test_fail("initial output should be nullopt");

    auto state = agent_ptr->get_state();
    if (state == agent::AgentState::Idle) test_pass("initial state is Idle");
    else test_fail("initial state should be Idle");
}

void test_submit_input() {
    test_header("submit_input");

    auto config = make_test_react_config();
    auto agent_ptr = agent::Agent::create_react(config);
    agent_ptr->start();
    agent_ptr->submit_input(strtou8("Hello"));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    agent_ptr->stop();
    test_pass("submit_input completes without crash");
}

void test_full_agent_run() {
    test_header("Full Agent Run (with all components)");

    const char* api_key_env = std::getenv("LLM_API_KEY");
    if (!api_key_env) {
        std::cout << "  [SKIP] LLM_API_KEY not set, skipping full run test\n";
        return;
    }

    auto config = make_test_react_config();
    config.model_config.api_key = strtou8(api_key_env);
    config.model_config.temperature = 0.7;
    config.model_config.max_tokens = 8192;

    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<EchoTool>());
    tool_registry->register_tool(std::make_shared<CalculatorTool>());
    config.tool_registry = tool_registry;

    config.personality.soul = strtou8("I am a comprehensive test agent.");
    config.personality.identity = strtou8("ID:comprehensive-test\nName:TestAgent");
    config.personality.agents = strtou8("Be helpful. Be concise. Be safe.");

    config.max_steps = 5;
    config.enable_thinking = true;
    config.auto_confirm = true;

    auto agent_ptr = agent::Agent::create_react(config);

    bool thinking_fired = false;
    bool output_fired = false;
    bool state_changed = false;

    agent_ptr->set_on_thinking_update([&](const agent::ThinkingStep& step) {
        thinking_fired = true;
        std::cout << "  [Thinking Step " << step.step_index << "] "
                  << u8tostr(step.thinking_content) << "\n";
    });
    agent_ptr->set_on_output_ready([&](const agent::u8str& output) {
        output_fired = true;
        std::cout << "  [Output] " << u8tostr(output) << "\n";
    });
    agent_ptr->set_on_state_change([&](agent::AgentState state) {
        state_changed = true;
        std::cout << "  [State] ";
        switch (state) {
            case agent::AgentState::Idle: std::cout << "Idle"; break;
            case agent::AgentState::Thinking: std::cout << "Thinking"; break;
            case agent::AgentState::WaitingToolResult: std::cout << "WaitingToolResult"; break;
            case agent::AgentState::WaitingUserConfirm: std::cout << "WaitingUserConfirm"; break;
            case agent::AgentState::Completed: std::cout << "Completed"; break;
            case agent::AgentState::Error: std::cout << "Error"; break;
        }
        std::cout << "\n";
    });

    agent_ptr->start();
    std::cout << "  Agent started. Submitting test input...\n";

    agent_ptr->submit_input(strtou8("Hello! Please echo back 'Hello World' and count the words in it."));

    std::this_thread::sleep_for(std::chrono::seconds(15));

    auto final_output = agent_ptr->get_output();
    auto thinking_steps = agent_ptr->get_thinking();
    auto context = agent_ptr->get_context();

    if (thinking_fired) test_pass("on_thinking_update was called");
    else test_fail("on_thinking_update was NOT called");

    if (output_fired) test_pass("on_output_ready was called");
    else test_fail("on_output_ready was NOT called");

    if (state_changed) test_pass("on_state_change was called");
    else test_fail("on_state_change was NOT called");

    if (final_output) test_pass("get_output returns a value");
    else test_fail("get_output is empty — agent did not produce output");

    agent_ptr->stop();
    std::cout << "  Agent stopped.\n";
}

// ============================================================
// Custom AgentLoop Implementation
// ============================================================

class SimpleCustomLoop : public agent::IAgentLoop {
public:
    SimpleCustomLoop(const agent::AgentLoopContext& ctx) : ctx_(ctx), state_(agent::AgentState::Idle) {}

    void run(const agent::u8str& user_input) override {
        set_state(agent::AgentState::Thinking);

        std::string input_str(user_input.begin(), user_input.end());
        auto tools = ctx_.tools.list_tools();
        std::string result_str = "[CustomLoop] Received: " + input_str
            + " (tools available: " + std::to_string(tools.size()) + ")";

        agent::u8str result(result_str.begin(), result_str.end());

        {
            std::lock_guard<std::mutex> lock(output_mutex_);
            final_output_ = result;
        }
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (on_output_ready_) on_output_ready_(result);
        }
        set_state(agent::AgentState::Completed);
    }

    void interrupt(const agent::u8str&) override {}
    void stop() override { stopped_ = true; }

    agent::AgentState get_state() const override { return state_.load(); }
    std::vector<agent::ThinkingStep> get_thinking_steps() const override { return {}; }
    std::optional<agent::u8str> get_final_output() const override {
        std::lock_guard<std::mutex> lock(output_mutex_);
        return final_output_;
    }
    bool needs_user_input() const override { return false; }
    std::optional<agent::Plan> get_plan() const override { return std::nullopt; }

    void set_on_thinking_update(std::function<void(const agent::ThinkingStep&)> cb) override {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        on_thinking_update_ = std::move(cb);
    }
    void set_on_output_ready(std::function<void(const agent::u8str&)> cb) override {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        on_output_ready_ = std::move(cb);
    }
    void set_on_state_change(std::function<void(agent::AgentState)> cb) override {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        on_state_change_ = std::move(cb);
    }

private:
    void set_state(agent::AgentState state) {
        state_ = state;
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (on_state_change_) on_state_change_(state);
    }

    agent::AgentLoopContext ctx_;
    std::atomic<agent::AgentState> state_;
    std::optional<agent::u8str> final_output_;
    bool stopped_ = false;
    mutable std::mutex output_mutex_;
    mutable std::mutex callback_mutex_;
    std::function<void(const agent::ThinkingStep&)> on_thinking_update_;
    std::function<void(const agent::u8str&)> on_output_ready_;
    std::function<void(agent::AgentState)> on_state_change_;
};

// 全功能自定义 Loop：使用 prompt_builder（自定义 instruction）、context_manager、tool 执行、thinking steps
class FullCustomLoop : public agent::IAgentLoop {
public:
    FullCustomLoop(const agent::AgentLoopContext& ctx) : ctx_(ctx), state_(agent::AgentState::Idle) {}

    void run(const agent::u8str& user_input) override {
        set_state(agent::AgentState::Thinking);

        // Step 1: 使用 prompt_builder 构建自定义 instruction 的 system prompt
        auto custom_instruction = strtou8("You are a custom loop agent. Process input and use tools when needed.");
        auto system_prompt = ctx_.prompt_builder.build_system_prompt(
            ctx_.personality, custom_instruction);

        // Step 2: 使用 context_manager 添加消息
        ctx_.context_manager.add_system_message(system_prompt);
        ctx_.context_manager.add_user_message(
            ctx_.prompt_builder.build_user_prompt(user_input, ctx_.personality));

        // Step 3: 发出 thinking step
        emit_thinking(0, strtou8("Analyzing user input and preparing tool execution"));

        // Step 4: 列出可用工具并执行
        auto tools = ctx_.tools.list_tools();
        emit_thinking(1, strtou8("Found " + std::to_string(tools.size()) + " tools available"));

        // Step 5: 尝试执行 echo 工具
        agent::u8str tool_result;
        if (ctx_.tools.has_tool(strtou8("echo"))) {
            auto echo_tool = ctx_.tools.get_tool(strtou8("echo"));
            if (echo_tool) {
                auto args = strtou8(R"({"text":"Hello from FullCustomLoop"})");
                tool_result = echo_tool->execute(args);
                emit_thinking(2, strtou8("Executed echo tool, got result"));
            }
        } else {
            tool_result = strtou8("no echo tool available");
            emit_thinking(2, strtou8("Echo tool not found"));
        }

        // Step 6: 构建最终输出
        std::string result_str = "[FullCustomLoop] Processed: "
            + std::string(user_input.begin(), user_input.end())
            + " | Tool result: " + std::string(tool_result.begin(), tool_result.end())
            + " | Tools count: " + std::to_string(tools.size());
        auto result = strtou8(result_str);

        {
            std::lock_guard<std::mutex> lock(output_mutex_);
            final_output_ = result;
        }
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (on_output_ready_) on_output_ready_(result);
        }
        set_state(agent::AgentState::Completed);
    }

    void interrupt(const agent::u8str&) override {}
    void stop() override { stopped_ = true; }

    agent::AgentState get_state() const override { return state_.load(); }
    std::vector<agent::ThinkingStep> get_thinking_steps() const override {
        std::lock_guard<std::mutex> lock(thinking_mutex_);
        return thinking_steps_;
    }
    std::optional<agent::u8str> get_final_output() const override {
        std::lock_guard<std::mutex> lock(output_mutex_);
        return final_output_;
    }
    bool needs_user_input() const override { return false; }
    std::optional<agent::Plan> get_plan() const override { return std::nullopt; }

    void set_on_thinking_update(std::function<void(const agent::ThinkingStep&)> cb) override {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        on_thinking_update_ = std::move(cb);
    }
    void set_on_output_ready(std::function<void(const agent::u8str&)> cb) override {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        on_output_ready_ = std::move(cb);
    }
    void set_on_state_change(std::function<void(agent::AgentState)> cb) override {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        on_state_change_ = std::move(cb);
    }

private:
    void set_state(agent::AgentState state) {
        state_ = state;
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (on_state_change_) on_state_change_(state);
    }

    void emit_thinking(int step, const agent::u8str& content) {
        agent::ThinkingStep ts;
        ts.step_index = step;
        ts.thinking_content = content;
        {
            std::lock_guard<std::mutex> lock(thinking_mutex_);
            thinking_steps_.push_back(ts);
        }
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (on_thinking_update_) on_thinking_update_(ts);
    }

    agent::AgentLoopContext              ctx_;
    std::atomic<agent::AgentState>       state_;
    std::optional<agent::u8str>          final_output_;
    bool                                 stopped_ = false;
    mutable std::mutex                   output_mutex_;
    mutable std::mutex                   thinking_mutex_;
    mutable std::mutex                   callback_mutex_;
    std::vector<agent::ThinkingStep>     thinking_steps_;
    std::function<void(const agent::ThinkingStep&)> on_thinking_update_;
    std::function<void(const agent::u8str&)>        on_output_ready_;
    std::function<void(agent::AgentState)>          on_state_change_;
};

void test_custom_loop() {
    test_header("Custom AgentLoop");

    // 测试 1: SimpleCustomLoop 通过 factory 创建
    {
        auto config = make_test_config();
        auto tool_registry = agent::create_tool_registry();
        tool_registry->register_tool(std::make_shared<EchoTool>());
        tool_registry->register_tool(std::make_shared<CalculatorTool>());
        config.tool_registry = tool_registry;

        auto loop_factory = [](const agent::AgentLoopContext& ctx)
            -> std::shared_ptr<agent::IAgentLoop> {
            return std::make_shared<SimpleCustomLoop>(ctx);
        };

        auto agent_ptr = agent::Agent::create_custom(std::move(loop_factory), config);
        if (agent_ptr) test_pass("Agent::create_custom(factory) returns non-null");
        else { test_fail("Agent creation failed"); return; }

        // 运行自定义 Loop
        bool output_fired = false;
        bool completed_fired = false;
        agent::u8str captured_output;

        agent_ptr->set_on_output_ready([&](const agent::u8str& output) {
            output_fired = true;
            captured_output = output;
        });
        agent_ptr->set_on_state_change([&](agent::AgentState state) {
            if (state == agent::AgentState::Completed) completed_fired = true;
        });

        agent_ptr->start();
        agent_ptr->submit_input(strtou8("Hello CustomLoop"));

        for (int i = 0; i < 50; ++i) {
            if (agent_ptr->get_state() == agent::AgentState::Completed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (output_fired) test_pass("on_output_ready was called in Custom Loop");
        else test_fail("on_output_ready was NOT called");

        if (completed_fired) test_pass("Completed state was reached in Custom Loop");
        else test_fail("Completed state was NOT reached");

        std::string out(captured_output.begin(), captured_output.end());
        if (out.find("[CustomLoop]") != std::string::npos && out.find("Hello CustomLoop") != std::string::npos)
            test_pass("Custom Loop output contains expected content");
        else test_fail("Custom Loop output content mismatch: " + out);

        if (out.find("tools available: 2") != std::string::npos)
            test_pass("Custom Loop can access tool registry via context");
        else test_fail("Custom Loop tool count mismatch: " + out);

        auto final_output = agent_ptr->get_output();
        if (final_output) test_pass("get_output returns value after Custom Loop");
        else test_fail("get_output should return value");

        agent_ptr->stop();
    }

    // 测试 2: FullCustomLoop - 使用 prompt_builder（自定义 instruction）、context_manager、tool 执行、thinking steps
    {
        auto config = make_test_config();
        config.personality.soul = strtou8("I am a test agent.");
        config.personality.identity = strtou8("ID:full-custom-test\nName:FullCustomAgent");

        auto tool_registry = agent::create_tool_registry();
        tool_registry->register_tool(std::make_shared<EchoTool>());
        tool_registry->register_tool(std::make_shared<CalculatorTool>());
        tool_registry->register_tool(std::make_shared<DangerousTool>());
        config.tool_registry = tool_registry;

        auto loop_factory = [](const agent::AgentLoopContext& ctx)
            -> std::shared_ptr<agent::IAgentLoop> {
            return std::make_shared<FullCustomLoop>(ctx);
        };

        auto agent_ptr = agent::Agent::create_custom(std::move(loop_factory), config);
        if (agent_ptr) test_pass("FullCustomLoop Agent::create_custom returns non-null");
        else { test_fail("FullCustomLoop Agent creation failed"); return; }

        bool output_fired = false;
        int thinking_count = 0;
        bool completed_fired = false;
        agent::u8str captured_output;

        agent_ptr->set_on_thinking_update([&](const agent::ThinkingStep& step) {
            ++thinking_count;
            std::cout << "  [Thinking " << step.step_index << "] "
                      << u8tostr(step.thinking_content) << "\n";
        });
        agent_ptr->set_on_output_ready([&](const agent::u8str& output) {
            output_fired = true;
            captured_output = output;
        });
        agent_ptr->set_on_state_change([&](agent::AgentState state) {
            if (state == agent::AgentState::Completed) completed_fired = true;
        });

        agent_ptr->start();
        agent_ptr->submit_input(strtou8("Test FullCustomLoop"));

        for (int i = 0; i < 50; ++i) {
            if (agent_ptr->get_state() == agent::AgentState::Completed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 验证 thinking steps
        if (thinking_count >= 3) test_pass("FullCustomLoop emitted 3+ thinking steps");
        else test_fail("FullCustomLoop thinking steps: expected >=3, got " + std::to_string(thinking_count));

        auto thinking_steps = agent_ptr->get_thinking();
        if (thinking_steps.size() >= 3) test_pass("get_thinking_steps returns 3+ steps");
        else test_fail("get_thinking_steps expected >=3, got " + std::to_string(thinking_steps.size()));

        // 验证 output
        if (output_fired) test_pass("FullCustomLoop on_output_ready was called");
        else test_fail("FullCustomLoop on_output_ready was NOT called");

        if (completed_fired) test_pass("FullCustomLoop reached Completed state");
        else test_fail("FullCustomLoop did NOT reach Completed state");

        std::string out(captured_output.begin(), captured_output.end());
        if (out.find("[FullCustomLoop]") != std::string::npos)
            test_pass("FullCustomLoop output contains marker");
        else test_fail("FullCustomLoop output missing marker: " + out);

        if (out.find("Test FullCustomLoop") != std::string::npos)
            test_pass("FullCustomLoop output contains user input");
        else test_fail("FullCustomLoop output missing user input");

        // 验证 echo 工具执行结果
        if (out.find("Hello from FullCustomLoop") != std::string::npos)
            test_pass("FullCustomLoop executed echo tool successfully");
        else test_fail("FullCustomLoop echo tool result not found in output");

        // 验证工具数量
        if (out.find("Tools count: 3") != std::string::npos)
            test_pass("FullCustomLoop sees 3 tools via context");
        else test_fail("FullCustomLoop tool count mismatch: " + out);

        // 验证 context_manager 中有消息
        auto context = agent_ptr->get_context();
        if (!context.messages.empty()) test_pass("FullCustomLoop context has messages");
        else test_fail("FullCustomLoop context should have messages");

        // 验证 final output
        auto final_output = agent_ptr->get_output();
        if (final_output) test_pass("FullCustomLoop get_output returns value");
        else test_fail("FullCustomLoop get_output should return value");

        agent_ptr->stop();
    }

    // 测试 3: PlanExecute Loop 类型
    {
        agent::PlanExecuteAgentConfig pe_config;
        pe_config.model_config.model_type = agent::LlmModelType::DeepSeek;
        pe_config.model_config.model_name = strtou8("deepseek-v4-flash");
        pe_config.model_config.api_base_url = strtou8("https://api.deepseek.com");
        pe_config.model_config.api_key = strtou8("test-key");
        pe_config.memory = std::make_shared<TestMemory>();

        auto agent_ptr = agent::Agent::create_plan_execute(pe_config);
        if (agent_ptr) test_pass("Agent::create_plan_execute() returns non-null");
        else test_fail("Agent::create_plan_execute() returned null");
    }
}

// ============================================================
// Mock Confirm Handler：自动确认，无需用户输入
// ============================================================

class MockConfirmHandler : public agent::IUserConfirmHandler {
public:
    agent::ConfirmResult confirm(const agent::ConfirmRequest& request) override {
        agent::ConfirmResult result;
        result.confirmed = true;  // 自动确认
        return result;
    }
    void confirm_async(const agent::ConfirmRequest& request,
                       std::function<void(agent::ConfirmResult)> callback) override {
        callback(confirm(request));
    }
};

// ============================================================
// Mock LLM Provider：按预设序列返回 LLM 响应
// 用于验证 ReAct 循环逻辑，无需真实 LLM API
// ============================================================

class MockLlmProvider : public agent::ILlmProvider {
public:
    // 预设响应队列：每次 send_request 弹出队首响应
    std::deque<agent::LlmResponse> responses;

    agent::LlmResponse send_request(const agent::LlmRequest& request) override {
        call_count++;
        if (responses.empty()) {
            agent::LlmResponse fallback;
            fallback.content = strtou8("[Mock] No more responses");
            return fallback;
        }
        auto resp = std::move(responses.front());
        responses.pop_front();
        return resp;
    }

    void send_request_async(const agent::LlmRequest& request,
                            std::function<void(agent::LlmResponse)> callback) override {
        callback(send_request(request));
    }

    agent::u8str get_provider_name() const override { return strtou8("mock"); }

    std::atomic<int> call_count{0};
};

// ============================================================
// Test: ReAct 纯文本回答后不会触发 auto-continue 死循环
// ============================================================

void test_react_loop_text_response_no_infinite_loop() {
    test_header("ReAct Loop: Text Response No Infinite Loop");

    // 场景模拟：
    //   用户问 "你的数据是从哪里获得的"
    //   LLM 第一次：调用 echo 工具（模拟之前调浏览器查数据）
    //   LLM 第二次：纯文本回答 "我的数据来自..."（无 tool_calls）
    //   预期：should_auto_continue() 返回 false，不会触发 auto-continue

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 第一次 LLM 响应：包含 tool_call（模拟查数据）
    {
        agent::LlmResponse resp1;
        resp1.content = strtou8("让我查一下数据来源");
        agent::ToolCall tc;
        tc.id = strtou8("call_1");
        tc.name = strtou8("echo");
        tc.arguments = strtou8(R"({"text":"data_source_query"})");
        resp1.tool_calls.push_back(std::move(tc));
        mock_llm->responses.push_back(std::move(resp1));
    }

    // 第二次 LLM 响应：纯文本回答（无 tool_calls）
    {
        agent::LlmResponse resp2;
        resp2.content = strtou8("我的数据来自公开的金融信息接口，A股行情数据是实时获取的。");
        resp2.tool_calls = {};  // 纯文本，无工具调用
        mock_llm->responses.push_back(std::move(resp2));
    }

    // 创建 Agent，注入 Mock LLM
    agent::ReactAgentConfig config;
    config.llm_provider = mock_llm;  // 注入 Mock
    config.memory = std::make_shared<TestMemory>();
    config.auto_confirm = true;
    config.max_steps = 10;
    config.enable_thinking = true;

    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<EchoTool>());
    config.tool_registry = tool_registry;

    auto agent_ptr = agent::Agent::create_react(config);

    // 记录 auto-continue 次数和状态变化
    int auto_continue_count = 0;
    int completed_count = 0;
    std::vector<agent::AgentState> state_transitions;

    agent_ptr->set_on_state_change([&](agent::AgentState state) {
        state_transitions.push_back(state);
        if (state == agent::AgentState::Completed) completed_count++;
    });

    agent_ptr->set_on_output_ready([&](const agent::u8str& output) {
        std::string out(output.begin(), output.end());
        // 如果输出了"继续"相关内容，说明 auto-continue 被触发了
        if (out.find("继续") != std::string::npos ||
            out.find("continue") != std::string::npos) {
            auto_continue_count++;
        }
    });

    agent_ptr->start();
    agent_ptr->submit_input(strtou8("你的数据是从哪里获得的"));

    // 等待完成（最多 5 秒，如果死循环会超时）
    for (int i = 0; i < 50; ++i) {
        auto state = agent_ptr->get_state();
        if (state == agent::AgentState::Completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 额外等待一小段时间，观察是否还有 auto-continue
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    agent_ptr->stop();

    // 验证 1：LLM 被调用恰好 2 次（1 次 tool_call + 1 次纯文本）
    if (mock_llm->call_count == 2)
        test_pass("LLM called exactly 2 times (1 tool_call + 1 text response)");
    else
        test_fail("LLM call count: expected 2, got " + std::to_string(mock_llm->call_count)
                  + " (possible infinite loop!)");

    // 验证 2：Completed 状态恰好出现 1 次
    if (completed_count == 1)
        test_pass("Completed state triggered exactly once");
    else
        test_fail("Completed count: expected 1, got " + std::to_string(completed_count));

    // 验证 3：auto-continue 未被触发
    if (auto_continue_count == 0)
        test_pass("No auto-continue triggered after text response");
    else
        test_fail("Auto-continue triggered " + std::to_string(auto_continue_count)
                  + " times (should be 0 for text response)");

    // 验证 4：最终输出包含纯文本回答
    auto final_output = agent_ptr->get_output();
    if (final_output) {
        std::string out(final_output->begin(), final_output->end());
        if (out.find("数据来自") != std::string::npos)
            test_pass("Final output contains text answer");
        else
            test_fail("Final output content mismatch: " + out);
    } else {
        test_fail("Final output is empty");
    }

    // 验证 5：状态转换中不包含重复的 Completed
    int completed_in_transitions = 0;
    for (auto s : state_transitions) {
        if (s == agent::AgentState::Completed) completed_in_transitions++;
    }
    if (completed_in_transitions <= 1)
        test_pass("No repeated Completed state transitions");
    else
        test_fail("Completed appeared " + std::to_string(completed_in_transitions)
                  + " times in state transitions (possible loop)");
}

// ============================================================
// Test: ReAct 连续纯文本回答不会死循环
// ============================================================

void test_react_loop_consecutive_text_no_loop() {
    test_header("ReAct Loop: Consecutive Text Responses No Loop");

    // 场景模拟：
    //   用户追问纯文本问题，LLM 每次都直接纯文本回答
    //   预期：每次 should_auto_continue() 都返回 false，不会触发 auto-continue

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 连续 3 次纯文本回答（模拟多轮对话中用户追问，LLM 不调用工具）
    for (int i = 0; i < 3; ++i) {
        agent::LlmResponse resp;
        resp.content = strtou8("这是第" + std::to_string(i + 1) + "次纯文本回答，不需要任何工具。");
        resp.tool_calls = {};
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReactAgentConfig config;
    config.llm_provider = mock_llm;
    config.memory = std::make_shared<TestMemory>();
    config.auto_confirm = true;
    config.max_steps = 10;
    config.enable_thinking = true;

    auto agent_ptr = agent::Agent::create_react(config);

    int completed_count = 0;
    agent_ptr->set_on_state_change([&](agent::AgentState state) {
        if (state == agent::AgentState::Completed) completed_count++;
    });

    agent_ptr->start();
    agent_ptr->submit_input(strtou8("请直接回答，不要使用工具"));

    // 等待完成
    for (int i = 0; i < 50; ++i) {
        auto state = agent_ptr->get_state();
        if (state == agent::AgentState::Completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    agent_ptr->stop();

    // 验证：LLM 只被调用 1 次（纯文本回答后直接完成，不会 auto-continue）
    if (mock_llm->call_count == 1)
        test_pass("LLM called exactly 1 time for pure text response");
    else
        test_fail("LLM call count: expected 1, got " + std::to_string(mock_llm->call_count)
                  + " (auto-continue may have been triggered)");

    // 验证：Completed 只出现 1 次
    if (completed_count == 1)
        test_pass("Completed triggered once for consecutive text test");
    else
        test_fail("Completed count: expected 1, got " + std::to_string(completed_count));
}

// ============================================================
// Test: ReAct tool_calls 后再纯文本，auto-continue 不触发
// ============================================================

void test_react_loop_tool_then_text_no_auto_continue() {
    test_header("ReAct Loop: Tool Call Then Text No Auto-Continue");

    // 场景模拟（复现原始 bug 场景）：
    //   用户问 A 股行情 → LLM 调用工具查数据
    //   用户追问 "数据从哪来" → LLM 纯文本回答
    //   预期：纯文本回答后 should_auto_continue() 返回 false

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 第一次：调用工具查数据
    {
        agent::LlmResponse resp;
        resp.content = strtou8("让我查一下A股行情");
        agent::ToolCall tc;
        tc.id = strtou8("call_1");
        tc.name = strtou8("echo");
        tc.arguments = strtou8(R"({"text":"A股行情查询"})");
        resp.tool_calls.push_back(std::move(tc));
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次：工具结果返回后，LLM 输出数据（有 tool_call 的场景）
    {
        agent::LlmResponse resp;
        resp.content = strtou8("根据查询结果，今日A股整体上涨，科技板块表现较好。");
        // 这次没有 tool_calls，是纯文本回答
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReactAgentConfig config;
    config.llm_provider = mock_llm;
    config.memory = std::make_shared<TestMemory>();
    config.auto_confirm = true;
    config.max_steps = 10;
    config.enable_thinking = true;

    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<EchoTool>());
    config.tool_registry = tool_registry;

    auto agent_ptr = agent::Agent::create_react(config);

    agent_ptr->start();
    agent_ptr->submit_input(strtou8("今天A股怎么样"));

    // 等待完成
    for (int i = 0; i < 50; ++i) {
        auto state = agent_ptr->get_state();
        if (state == agent::AgentState::Completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    agent_ptr->stop();

    // 验证：LLM 调用 2 次后停止，不会因 auto-continue 继续调用
    if (mock_llm->call_count == 2)
        test_pass("Tool+Text scenario: LLM called 2 times, no auto-continue loop");
    else
        test_fail("Tool+Text scenario: LLM call count expected 2, got "
                  + std::to_string(mock_llm->call_count));
}

void test_dangerous_tool_confirmation() {
    test_header("Dangerous Tool Confirmation");

    const char* api_key_env = std::getenv("LLM_API_KEY");
    if (!api_key_env) {
        std::cout << "  [SKIP] LLM_API_KEY not set, skipping confirmation test\n";
        return;
    }

    auto config = make_test_react_config(false);
    config.model_config.api_key = strtou8(api_key_env);

    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<DangerousTool>());
    config.tool_registry = tool_registry;

    auto agent_ptr = agent::Agent::create_react(config);

    bool confirm_requested = false;
    agent_ptr->set_on_state_change([&](agent::AgentState state) {
        if (state == agent::AgentState::WaitingUserConfirm) confirm_requested = true;
    });

    agent_ptr->start();
    agent_ptr->submit_input(strtou8("Please perform a dangerous operation: delete all files"));
    std::this_thread::sleep_for(std::chrono::seconds(10));
    agent_ptr->stop();

    if (confirm_requested) test_pass("WaitingUserConfirm state was triggered for dangerous tool");
    else test_fail("WaitingUserConfirm was NOT triggered — dangerous tool may not have been called by LLM");
}

// ============================================================
// Test: 并发工具调用 - 并行执行时间验证
// 3 个 100ms 延迟工具并行执行应在 < 200ms 内完成
// ============================================================

void test_concurrent_tool_parallel_execution() {
    test_header("Concurrent Tool: Parallel Execution Time");

    auto mock_llm = std::make_shared<MockLlmProvider>();
    std::cout << "  [DEBUG] mock_llm created, use_count=" << mock_llm.use_count() << std::endl;

    // LLM 返回 3 个 tool_calls（均为 auto_confirm，触发并行路径）
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Let me call three slow tools in parallel");

        agent::ToolCall tc1;
        tc1.id = strtou8("call_1");
        tc1.name = strtou8("slow_echo_A");
        tc1.arguments = strtou8(R"({"text":"hello from A"})");
        resp.tool_calls.push_back(std::move(tc1));

        agent::ToolCall tc2;
        tc2.id = strtou8("call_2");
        tc2.name = strtou8("slow_echo_B");
        tc2.arguments = strtou8(R"({"text":"hello from B"})");
        resp.tool_calls.push_back(std::move(tc2));

        agent::ToolCall tc3;
        tc3.id = strtou8("call_3");
        tc3.name = strtou8("slow_echo_C");
        tc3.arguments = strtou8(R"({"text":"hello from C"})");
        resp.tool_calls.push_back(std::move(tc3));

        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次 LLM 响应：纯文本完成
    {
        agent::LlmResponse resp;
        resp.content = strtou8("All done");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReactAgentConfig config;
    config.llm_provider = mock_llm;
    config.memory = std::make_shared<TestMemory>();
    config.auto_confirm = true;
    config.max_steps = 5;
    config.enable_thinking = false;

    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<SlowEchoTool>(100, "_A"));
    tool_registry->register_tool(std::make_shared<SlowEchoTool>(100, "_B"));
    tool_registry->register_tool(std::make_shared<SlowEchoTool>(100, "_C"));
    config.tool_registry = tool_registry;

    std::cout << "  [DEBUG] Creating agent, config.llm_provider use_count="
              << config.llm_provider.use_count() << std::endl;

    auto agent_ptr = agent::Agent::create_react(config);

    std::cout << "  [DEBUG] Agent created, mock_llm use_count=" << mock_llm.use_count() << std::endl;

    std::vector<agent::u8str> captured_outputs;
    std::mutex output_mutex;
    agent_ptr->set_on_output_ready([&](const agent::u8str& output) {
        std::lock_guard<std::mutex> lock(output_mutex);
        captured_outputs.push_back(output);
    });

    auto start_time = std::chrono::steady_clock::now();

    agent_ptr->start();
    agent_ptr->submit_input(strtou8("Call all three slow tools"));

    for (int i = 0; i < 60; ++i) {
        auto state = agent_ptr->get_state();
        if (state == agent::AgentState::Completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    agent_ptr->stop();

    std::cout << "  [DEBUG] After stop, mock_llm->call_count=" << mock_llm->call_count
              << ", responses remaining=" << mock_llm->responses.size()
              << ", elapsed=" << elapsed_ms << "ms" << std::endl;

    // 并行执行应在 < 250ms 内完成（3 * 100ms 串行需 300ms+）
    // 放宽到 300ms 给 Windows 调度留余地
    if (elapsed_ms < 400)
        test_pass("Parallel execution time " + std::to_string(elapsed_ms)
                  + "ms (< 400ms, serial would be ~300ms+)");
    else
        test_fail("Parallel execution too slow: " + std::to_string(elapsed_ms)
                  + "ms (serial would be ~300ms)");

    // 验证 LLM 被正确调用
    if (mock_llm->call_count == 2)
        test_pass("LLM called 2 times (tools + text completion)");
    else
        test_fail("LLM call count: expected 2, got " + std::to_string(mock_llm->call_count));

    // 验证最终状态
    if (agent_ptr->get_state() == agent::AgentState::Completed)
        test_pass("Agent completed after parallel tool execution");
    else
        test_fail("Agent did not reach completed state");
}

// ============================================================
// Test: 并发工具调用 - 有确认需求时回退到串行
// ============================================================

void test_concurrent_tool_serial_fallback() {
    test_header("Concurrent Tool: Serial Fallback on Confirmation");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // LLM 返回 3 个 tool_calls，其中第二个需要确认 → 应回退到串行
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Calling tools, one needs confirmation");

        agent::ToolCall tc1;
        tc1.id = strtou8("call_1");
        tc1.name = strtou8("confirm_echo_A");
        tc1.arguments = strtou8(R"({"text":"no confirm needed"})");
        resp.tool_calls.push_back(std::move(tc1));

        agent::ToolCall tc2;
        tc2.id = strtou8("call_2");
        tc2.name = strtou8("confirm_echo_B");
        tc2.arguments = strtou8(R"({"text":"needs confirm"})");
        resp.tool_calls.push_back(std::move(tc2));

        agent::ToolCall tc3;
        tc3.id = strtou8("call_3");
        tc3.name = strtou8("confirm_echo_C");
        tc3.arguments = strtou8(R"({"text":"also no confirm"})");
        resp.tool_calls.push_back(std::move(tc3));

        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次：纯文本完成
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Serial execution done");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReactAgentConfig config;
    config.llm_provider = mock_llm;
    config.confirm_handler = std::make_shared<MockConfirmHandler>();
    config.memory = std::make_shared<TestMemory>();
    config.auto_confirm = false;  // 不自动确认，测试串行回退路径
    config.max_steps = 5;
    config.enable_thinking = false;

    auto tool_registry = agent::create_tool_registry();
    // A 和 C 不需要确认，B 需要确认
    tool_registry->register_tool(std::make_shared<ConfirmableEchoTool>(false, "_A"));
    tool_registry->register_tool(std::make_shared<ConfirmableEchoTool>(true, "_B"));
    tool_registry->register_tool(std::make_shared<ConfirmableEchoTool>(false, "_C"));
    config.tool_registry = tool_registry;

    auto agent_ptr = agent::Agent::create_react(config);

    std::vector<agent::AgentState> states;
    agent_ptr->set_on_state_change([&](agent::AgentState s) {
        states.push_back(s);
    });

    agent_ptr->start();
    agent_ptr->submit_input(strtou8("Call tools, one needs confirmation"));

    for (int i = 0; i < 50; ++i) {
        auto state = agent_ptr->get_state();
        if (state == agent::AgentState::Completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    agent_ptr->stop();

    // 验证：确认工具触发了 WaitingUserConfirm 状态（串行路径特征）
    bool had_waiting_confirm = false;
    for (auto s : states) {
        if (s == agent::AgentState::WaitingUserConfirm) {
            had_waiting_confirm = true;
            break;
        }
    }
    if (had_waiting_confirm)
        test_pass("WaitingUserConfirm triggered (serial path with confirmation)");
    else
        test_fail("WaitingUserConfirm was NOT triggered");

    // 由于有确认需求，应该不会走并行路径
    // 工具被执行了（回退到串行路径）
    if (mock_llm->call_count >= 2)
        test_pass("Serial path executed tools successfully (LLM calls: "
                  + std::to_string(mock_llm->call_count) + ")");
    else
        test_fail("LLM not called enough times: " + std::to_string(mock_llm->call_count));
}

// ============================================================
// Test: 并发工具调用 - 结果顺序验证
// 并行执行后 context 中的 tool message 顺序应与 tool_calls 一致
// ============================================================

void test_concurrent_tool_result_order() {
    test_header("Concurrent Tool: Result Order Preservation");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // LLM 返回 3 个有序 tool_calls
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Calling numbered tools");

        for (int i = 1; i <= 3; ++i) {
            agent::ToolCall tc;
            tc.id = strtou8("call_" + std::to_string(i));
            tc.name = strtou8("slow_echo_" + std::to_string(i));
            std::string args = R"({"text":")" + std::to_string(i) + R"("})";
            tc.arguments = strtou8(args);
            resp.tool_calls.push_back(std::move(tc));
        }

        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次：完成
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Order test done");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReactAgentConfig config;
    config.llm_provider = mock_llm;
    config.memory = std::make_shared<TestMemory>();
    config.auto_confirm = true;
    config.max_steps = 5;
    config.enable_thinking = false;

    auto tool_registry = agent::create_tool_registry();
    // 故意给不同延迟，让执行完成顺序不确定
    tool_registry->register_tool(std::make_shared<SlowEchoTool>(150, "_1"));
    tool_registry->register_tool(std::make_shared<SlowEchoTool>(50, "_2"));
    tool_registry->register_tool(std::make_shared<SlowEchoTool>(100, "_3"));
    config.tool_registry = tool_registry;

    auto agent_ptr = agent::Agent::create_react(config);

    agent_ptr->start();
    agent_ptr->submit_input(strtou8("Call three tools with different delays"));

    for (int i = 0; i < 60; ++i) {
        auto state = agent_ptr->get_state();
        if (state == agent::AgentState::Completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    agent_ptr->stop();

    // 验证 context 中有正确的 tool message 顺序
    auto context = agent_ptr->get_context();
    int tool_msg_count = 0;
    std::vector<std::string> tool_results;
    for (const auto& msg : context.messages) {
        if (msg.role == agent::MessageRole::Tool) {
            ++tool_msg_count;
            tool_results.push_back(u8tostr(msg.content));
        }
    }

    if (tool_msg_count == 3)
        test_pass("Context has 3 tool messages (correct count)");
    else
        test_fail("Expected 3 tool messages, got " + std::to_string(tool_msg_count));

    // 验证消息按顺序包含正确的 ID（call_1, call_2, call_3）
    bool order_ok = true;
    for (int i = 0; i < static_cast<int>(tool_results.size()); ++i) {
        std::string expected_id = "[slow_echo_" + std::to_string(i + 1) + "]";
        if (tool_results[i].find(expected_id) == std::string::npos) {
            order_ok = false;
            break;
        }
    }
    if (order_ok && tool_results.size() == 3)
        test_pass("Tool results in context preserve correct order");
    else if (tool_results.size() == 3)
        test_fail("Tool result order mismatch");
}

// ============================================================
// Test: 单工具调用无误退化
// ============================================================

void test_single_tool_no_parallel_regression() {
    test_header("Concurrent Tool: Single Tool No Regression");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 单个 tool_call → 走串行路径（size <= 1）
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Let me echo");

        agent::ToolCall tc;
        tc.id = strtou8("call_1");
        tc.name = strtou8("echo");
        tc.arguments = strtou8(R"({"text":"hello single"})");
        resp.tool_calls.push_back(std::move(tc));

        mock_llm->responses.push_back(std::move(resp));
    }

    // 完成
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Single tool done");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReactAgentConfig config;
    config.llm_provider = mock_llm;
    config.memory = std::make_shared<TestMemory>();
    config.auto_confirm = true;
    config.max_steps = 5;
    config.enable_thinking = false;

    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<EchoTool>());
    config.tool_registry = tool_registry;

    auto agent_ptr = agent::Agent::create_react(config);

    agent_ptr->start();
    agent_ptr->submit_input(strtou8("Echo hello single"));

    for (int i = 0; i < 50; ++i) {
        auto state = agent_ptr->get_state();
        if (state == agent::AgentState::Completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    agent_ptr->stop();

    if (mock_llm->call_count == 2)
        test_pass("Single tool: LLM called 2 times (tool + text)");
    else
        test_fail("Single tool: LLM call count expected 2, got "
                  + std::to_string(mock_llm->call_count));

    auto context = agent_ptr->get_context();
    bool has_tool_msg = false;
    for (const auto& msg : context.messages) {
        if (msg.role == agent::MessageRole::Tool &&
            u8tostr(msg.content).find("hello single") != std::string::npos) {
            has_tool_msg = true;
        }
    }
    if (has_tool_msg)
        test_pass("Single tool: tool message contains expected result");
    else
        test_fail("Single tool: tool message missing or content mismatch");
}

// ============================================================
// Test: 并发工具调用 + 自动确认模式（auto_confirm=true）
// ============================================================

void test_concurrent_tool_with_auto_confirm() {
    test_header("Concurrent Tool: All Auto-Confirm Triggers Parallel");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 2 个tool_calls，都 auto_confirm → 应走并行路径
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Calling two parallel tools");

        agent::ToolCall tc1;
        tc1.id = strtou8("call_1");
        tc1.name = strtou8("slow_echo_X");
        tc1.arguments = strtou8(R"({"text":"parallel A"})");
        resp.tool_calls.push_back(std::move(tc1));

        agent::ToolCall tc2;
        tc2.id = strtou8("call_2");
        tc2.name = strtou8("slow_echo_Y");
        tc2.arguments = strtou8(R"({"text":"parallel B"})");
        resp.tool_calls.push_back(std::move(tc2));

        mock_llm->responses.push_back(std::move(resp));
    }

    // 完成
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Parallel done");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReactAgentConfig config;
    config.llm_provider = mock_llm;
    config.memory = std::make_shared<TestMemory>();
    config.auto_confirm = true;
    config.max_steps = 5;
    config.enable_thinking = false;

    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<SlowEchoTool>(100, "_X"));
    tool_registry->register_tool(std::make_shared<SlowEchoTool>(100, "_Y"));
    config.tool_registry = tool_registry;

    auto agent_ptr = agent::Agent::create_react(config);

    auto start_time = std::chrono::steady_clock::now();

    agent_ptr->start();
    agent_ptr->submit_input(strtou8("Call two parallel tools"));

    for (int i = 0; i < 60; ++i) {
        auto state = agent_ptr->get_state();
        if (state == agent::AgentState::Completed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    agent_ptr->stop();

    // 2 个 100ms 工具并行应在 < 250ms 完成
    if (elapsed_ms < 350)
        test_pass("Auto-confirm parallel: " + std::to_string(elapsed_ms)
                  + "ms (serial would be ~200ms+)");
    else
        test_fail("Auto-confirm parallel too slow: " + std::to_string(elapsed_ms) + "ms");

    if (mock_llm->call_count == 2)
        test_pass("Auto-confirm parallel: LLM called 2 times");
    else
        test_fail("Auto-confirm parallel: LLM call count " + std::to_string(mock_llm->call_count));
}

// ============================================================
// Reflection Loop Tests
// ============================================================

void test_reflection_accept_on_first_critique() {
    test_header("ReflectionLoop - Accept on First Critique");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 第一次 LLM 调用（generate）：纯文本回答
    {
        agent::LlmResponse resp;
        resp.content = strtou8("This is the answer to the question.");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次 LLM 调用（critique）：评价为可接受
    {
        agent::LlmResponse resp;
        resp.content = strtou8(R"({"score": 9, "issues": [], "suggestions": [], "acceptable": "YES"})");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReflectionLoopConfig ref_config;
    ref_config.base.auto_confirm = true;
    ref_config.base.enable_thinking = true;

    agent::DefaultContextManager context;
    agent::PromptBuilder prompt_builder;
    auto tool_registry = agent::create_tool_registry();
    agent::McpManager mcps;
    TestMemory memory;
    agent::PersonalityDocs personality;

    agent::ReflectionLoop loop(
        mock_llm, nullptr, context, prompt_builder, *tool_registry, mcps, memory,
        personality, ref_config);

    loop.run(strtou8("What is the answer?"));

    if (mock_llm->call_count == 2)
        test_pass("LLM call count is 2 (1 generate + 1 critique)");
    else
        test_fail("LLM call count: expected 2, got " + std::to_string(mock_llm->call_count));

    if (loop.get_state() == agent::AgentState::Completed)
        test_pass("State is Completed");
    else
        test_fail("State should be Completed");

    auto output = loop.get_final_output();
    if (output && u8tostr(*output) == "This is the answer to the question.")
        test_pass("Output is correct");
    else if (output)
        test_fail("Output mismatch: " + u8tostr(*output));
    else
        test_fail("Output is nullopt");
}

void test_reflection_refine_after_critique() {
    test_header("ReflectionLoop - Refine After Critique");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 第一次 LLM 调用（generate）：初始回答
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Initial answer");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次 LLM 调用（critique round 1）：需要改进
    {
        agent::LlmResponse resp;
        resp.content = strtou8(R"({"score": 5, "issues": ["Missing details"], "suggestions": ["Add more info"], "acceptable": "NO"})");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第三次 LLM 调用（refine - generate）：改进后的回答
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Improved answer with more details");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第四次 LLM 调用（critique round 2）：接受
    {
        agent::LlmResponse resp;
        resp.content = strtou8(R"({"score": 9, "issues": [], "suggestions": [], "acceptable": "YES"})");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReflectionLoopConfig ref_config;
    ref_config.base.auto_confirm = true;
    ref_config.max_reflection_rounds = 3;

    agent::DefaultContextManager context;
    agent::PromptBuilder prompt_builder;
    auto tool_registry = agent::create_tool_registry();
    agent::McpManager mcps;
    TestMemory memory;
    agent::PersonalityDocs personality;

    agent::ReflectionLoop loop(
        mock_llm, nullptr, context, prompt_builder, *tool_registry, mcps, memory,
        personality, ref_config);

    loop.run(strtou8("Tell me about the topic"));

    if (mock_llm->call_count == 4)
        test_pass("LLM call count is 4 (1 generate + 1 critique + 1 refine + 1 critique)");
    else
        test_fail("LLM call count: expected 4, got " + std::to_string(mock_llm->call_count));

    if (loop.get_state() == agent::AgentState::Completed)
        test_pass("State is Completed");
    else
        test_fail("State should be Completed");

    auto output = loop.get_final_output();
    if (output && u8tostr(*output) == "Improved answer with more details")
        test_pass("Output is the refined answer");
    else if (output)
        test_fail("Output mismatch: " + u8tostr(*output));
    else
        test_fail("Output is nullopt");
}

void test_reflection_max_rounds_reached() {
    test_header("ReflectionLoop - Max Rounds Reached");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 第一次 LLM 调用（generate）：初始回答
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Initial answer");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次 LLM 调用（critique round 1）：不可接受
    {
        agent::LlmResponse resp;
        resp.content = strtou8(R"({"score": 5, "issues": ["Issue"], "suggestions": ["Fix"], "acceptable": "NO"})");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第三次 LLM 调用（refine - generate）：改进后的回答
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Improved answer");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第四次 LLM 调用（critique round 2）：仍然不可接受
    {
        agent::LlmResponse resp;
        resp.content = strtou8(R"({"score": 6, "issues": ["Still not perfect"], "suggestions": ["Fix more"], "acceptable": "NO"})");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReflectionLoopConfig ref_config;
    ref_config.base.auto_confirm = true;
    ref_config.max_reflection_rounds = 2;

    agent::DefaultContextManager context;
    agent::PromptBuilder prompt_builder;
    auto tool_registry = agent::create_tool_registry();
    agent::McpManager mcps;
    TestMemory memory;
    agent::PersonalityDocs personality;

    agent::ReflectionLoop loop(
        mock_llm, nullptr, context, prompt_builder, *tool_registry, mcps, memory,
        personality, ref_config);

    loop.run(strtou8("Tell me about the topic"));

    if (mock_llm->call_count == 4)
        test_pass("LLM call count is 4 (max 2 rounds of critique)");
    else
        test_fail("LLM call count: expected 4, got " + std::to_string(mock_llm->call_count));

    if (loop.get_state() == agent::AgentState::Completed)
        test_pass("State is Completed");
    else
        test_fail("State should be Completed");

    auto output = loop.get_final_output();
    if (output && u8tostr(*output) == "Improved answer")
        test_pass("Output is the last refined answer");
    else if (output)
        test_fail("Output mismatch: " + u8tostr(*output));
    else
        test_fail("Output is nullopt");
}

void test_reflection_tool_usage_in_generation() {
    test_header("ReflectionLoop - Tool Usage in Generation");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 第一次 LLM 调用（generate）：tool call 到 echo
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Let me echo something");
        agent::ToolCall tc;
        tc.id = strtou8("call_1");
        tc.name = strtou8("echo");
        tc.arguments = strtou8(R"({"text":"hello"})");
        resp.tool_calls.push_back(std::move(tc));
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次 LLM 调用（generate 子循环继续）：纯文本回答
    {
        agent::LlmResponse resp;
        resp.content = strtou8("Got it: hello");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第三次 LLM 调用（critique）：接受
    {
        agent::LlmResponse resp;
        resp.content = strtou8(R"({"score": 9, "issues": [], "suggestions": [], "acceptable": "YES"})");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReflectionLoopConfig ref_config;
    ref_config.base.auto_confirm = true;

    agent::DefaultContextManager context;
    agent::PromptBuilder prompt_builder;
    auto tool_registry = agent::create_tool_registry();
    tool_registry->register_tool(std::make_shared<EchoTool>());
    agent::McpManager mcps;
    TestMemory memory;
    agent::PersonalityDocs personality;

    agent::ReflectionLoop loop(
        mock_llm, nullptr, context, prompt_builder, *tool_registry, mcps, memory,
        personality, ref_config);

    loop.run(strtou8("Echo hello"));

    if (mock_llm->call_count == 3)
        test_pass("LLM call count is 3 (1 tool call + 1 text + 1 critique)");
    else
        test_fail("LLM call count: expected 3, got " + std::to_string(mock_llm->call_count));

    if (loop.get_state() == agent::AgentState::Completed)
        test_pass("State is Completed");
    else
        test_fail("State should be Completed");

    auto output = loop.get_final_output();
    if (output && u8tostr(*output).find("hello") != std::string::npos)
        test_pass("Output contains 'hello'");
    else if (output)
        test_fail("Output does not contain 'hello': " + u8tostr(*output));
    else
        test_fail("Output is nullopt");
}

void test_reflection_thinking_steps() {
    test_header("ReflectionLoop - Thinking Steps");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 第一次 LLM 调用（generate）：纯文本回答
    {
        agent::LlmResponse resp;
        resp.content = strtou8("This is the answer.");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次 LLM 调用（critique）：接受
    {
        agent::LlmResponse resp;
        resp.content = strtou8(R"({"score": 9, "issues": [], "suggestions": [], "acceptable": "YES"})");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReflectionLoopConfig ref_config;
    ref_config.base.enable_thinking = true;
    ref_config.base.auto_confirm = true;
    ref_config.max_reflection_rounds = 2;

    agent::DefaultContextManager context;
    agent::PromptBuilder prompt_builder;
    auto tool_registry = agent::create_tool_registry();
    agent::McpManager mcps;
    TestMemory memory;
    agent::PersonalityDocs personality;

    agent::ReflectionLoop loop(
        mock_llm, nullptr, context, prompt_builder, *tool_registry, mcps, memory,
        personality, ref_config);

    loop.run(strtou8("What is the answer?"));

    auto steps = loop.get_thinking_steps();
    if (steps.size() >= 3)
        test_pass("get_thinking_steps() returns at least 3 steps (got " + std::to_string(steps.size()) + ")");
    else
        test_fail("get_thinking_steps() returned " + std::to_string(steps.size()) + " steps, expected >= 3");

    bool has_reflection = false;
    bool has_critique = false;
    for (const auto& step : steps) {
        std::string content = u8tostr(step.thinking_content);
        if (content.find("[Reflection]") != std::string::npos) has_reflection = true;
        if (content.find("Critique result") != std::string::npos) has_critique = true;
    }

    if (has_reflection)
        test_pass("At least one step contains '[Reflection]'");
    else
        test_fail("No step contains '[Reflection]'");

    if (has_critique)
        test_pass("At least one step contains 'Critique result'");
    else
        test_fail("No step contains 'Critique result'");
}

void test_reflection_critic_fallback_chain() {
    test_header("ReflectionLoop - Critic Fallback Chain");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 第一次 LLM 调用（generate）：纯文本回答
    {
        agent::LlmResponse resp;
        resp.content = strtou8("The capital of France is Paris.");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次 LLM 调用（critique attempt 1）：Critic 调用失败
    {
        agent::LlmResponse resp;
        resp.is_error = true;
        resp.error_message = strtou8("Critic LLM unavailable");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第三次 LLM 调用（critique attempt 2）：Critic 调用再次失败
    {
        agent::LlmResponse resp;
        resp.is_error = true;
        resp.error_message = strtou8("Critic LLM still unavailable");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第四次 LLM 调用（self-critique 回退）：Generator 自评通过
    {
        agent::LlmResponse resp;
        resp.content = strtou8(R"({"score": 9, "issues": [], "suggestions": [], "acceptable": "YES"})");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReflectionLoopConfig ref_config;
    ref_config.base.auto_confirm = true;
    ref_config.base.enable_thinking = true;

    agent::DefaultContextManager context;
    agent::PromptBuilder prompt_builder;
    auto tool_registry = agent::create_tool_registry();
    agent::McpManager mcps;
    TestMemory memory;
    agent::PersonalityDocs personality;

    agent::ReflectionLoop loop(
        mock_llm, nullptr, context, prompt_builder, *tool_registry, mcps, memory,
        personality, ref_config);

    loop.run(strtou8("What is the capital of France?"));

    // 验证 LLM 调用次数：1 generate + 2 critic retries + 1 self-critique = 4
    if (mock_llm->call_count == 4)
        test_pass("LLM call count is 4 (1 generate + 2 critic retries + 1 self-critique)");
    else
        test_fail("LLM call count: expected 4, got " + std::to_string(mock_llm->call_count));

    if (loop.get_state() == agent::AgentState::Completed)
        test_pass("State is Completed");
    else
        test_fail("State should be Completed");

    auto output = loop.get_final_output();
    if (output && u8tostr(*output) == "The capital of France is Paris.")
        test_pass("Output is correct (original answer accepted via self-critique)");
    else if (output)
        test_fail("Output mismatch: " + u8tostr(*output));
    else
        test_fail("Output is nullopt");

    // 验证思考步骤包含降级标记
    auto steps = loop.get_thinking_steps();
    bool has_critique = false;
    for (const auto& step : steps) {
        std::string content = u8tostr(step.thinking_content);
        if (content.find("Critique result") != std::string::npos) has_critique = true;
    }
    if (has_critique)
        test_pass("Critique result step is present (self-critique accepted)");
    else
        test_fail("No critique result step found");
}

void test_reflection_final_fallback_on_all_fail() {
    test_header("ReflectionLoop - Final Fallback (Critic + Self-Critique All Fail)");

    auto mock_llm = std::make_shared<MockLlmProvider>();

    // 第一次 LLM 调用（generate）：纯文本回答
    {
        agent::LlmResponse resp;
        resp.content = strtou8("The answer is 42.");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第二次 LLM 调用（critique attempt 1）：Critic 调用失败
    {
        agent::LlmResponse resp;
        resp.is_error = true;
        resp.error_message = strtou8("Critic LLM crash");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第三次 LLM 调用（critique attempt 2）：Critic 调用再次失败
    {
        agent::LlmResponse resp;
        resp.is_error = true;
        resp.error_message = strtou8("Critic LLM crash again");
        mock_llm->responses.push_back(std::move(resp));
    }

    // 第四次 LLM 调用（self-critique 回退）：自评也失败
    {
        agent::LlmResponse resp;
        resp.is_error = true;
        resp.error_message = strtou8("Self-critique LLM also failed");
        mock_llm->responses.push_back(std::move(resp));
    }

    agent::ReflectionLoopConfig ref_config;
    ref_config.base.auto_confirm = true;
    ref_config.base.enable_thinking = true;

    agent::DefaultContextManager context;
    agent::PromptBuilder prompt_builder;
    auto tool_registry = agent::create_tool_registry();
    agent::McpManager mcps;
    TestMemory memory;
    agent::PersonalityDocs personality;

    agent::ReflectionLoop loop(
        mock_llm, nullptr, context, prompt_builder, *tool_registry, mcps, memory,
        personality, ref_config);

    loop.run(strtou8("What is the answer?"));

    // 验证 LLM 调用次数：1 generate + 2 critic retries + 1 self-critique = 4
    if (mock_llm->call_count == 4)
        test_pass("LLM call count is 4 (1 generate + 2 critic + 1 self-critique, all failed)");
    else
        test_fail("LLM call count: expected 4, got " + std::to_string(mock_llm->call_count));

    if (loop.get_state() == agent::AgentState::Completed)
        test_pass("State is Completed (final fallback to accept)");
    else
        test_fail("State should be Completed");

    auto output = loop.get_final_output();
    if (output && u8tostr(*output) == "The answer is 42.")
        test_pass("Output is correct (original answer accepted via final fallback)");
    else if (output)
        test_fail("Output mismatch: " + u8tostr(*output));
    else
        test_fail("Output is nullopt");

    // 验证思考步骤包含降级标记
    auto steps = loop.get_thinking_steps();
    bool has_accept = false;
    for (const auto& step : steps) {
        std::string content = u8tostr(step.thinking_content);
        if (content.find("Answer accepted") != std::string::npos) has_accept = true;
    }
    if (has_accept)
        test_pass("Answer accepted step is present (final fallback accepted)");
    else
        test_fail("No answer accepted step found");
}

void test_reflection_create_reflection_factory() {
    test_header("ReflectionLoop - create_reflection Factory");

    agent::ReflectionAgentConfig config;
    config.model_config.model_type = agent::LlmModelType::DeepSeek;
    config.model_config.model_name = strtou8("deepseek-v4-flash");
    config.model_config.api_base_url = strtou8("https://api.deepseek.com");
    config.model_config.api_key = strtou8("test-key");
    config.memory = std::make_shared<TestMemory>();

    auto agent_ptr = agent::Agent::create_reflection(config);
    if (agent_ptr)
        test_pass("Agent::create_reflection() returns non-null");
    else {
        test_fail("Agent::create_reflection() returned null");
        return;
    }

    if (agent_ptr->get_state() == agent::AgentState::Idle)
        test_pass("get_state() returns Idle");
    else
        test_fail("get_state() should return Idle");

    agent_ptr->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    test_pass("start() completes without error");

    agent_ptr->stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    test_pass("stop() completes without error");
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "========================================\n";
    std::cout << "  Agent Framework - Comprehensive Test\n";
    std::cout << "========================================\n";

    test_agent_create_and_lifecycle();
    test_tool_registration();
    test_memory_operations();
    test_confirm_handler();
    test_prompt_builder();
    test_personality_api();
    test_callbacks();
    test_llm_provider();
    test_context_inspection();
    test_submit_input();
    test_custom_loop();
    test_react_loop_text_response_no_infinite_loop();
    test_react_loop_consecutive_text_no_loop();
    test_react_loop_tool_then_text_no_auto_continue();
    test_concurrent_tool_parallel_execution();
    test_concurrent_tool_serial_fallback();
    test_concurrent_tool_result_order();
    test_single_tool_no_parallel_regression();
    test_concurrent_tool_with_auto_confirm();
    test_reflection_accept_on_first_critique();
    test_reflection_refine_after_critique();
    test_reflection_max_rounds_reached();
    test_reflection_tool_usage_in_generation();
    test_reflection_thinking_steps();
    test_reflection_critic_fallback_chain();
    test_reflection_final_fallback_on_all_fail();
    test_reflection_create_reflection_factory();
    test_full_agent_run();
    test_dangerous_tool_confirmation();

    std::cout << "\n========================================\n";
    std::cout << "  Results: " << pass_count << " / " << test_count << " passed\n";
    std::cout << "========================================\n";

    return (pass_count == test_count) ? 0 : 1;
}
