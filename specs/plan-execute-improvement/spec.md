# Plan-and-Execute 架构改进方案

## 一、现状诊断

当前 `PlanExecuteLoop` 本质上是一个**线性步骤执行器**，而非真正的 Plan-and-Execute 架构。核心问题：

| 特征 | 评分 | 关键缺失 |
|------|------|----------|
| 高质量规划 | 4/10 | 步骤无依赖/参数/分支，PlanStep 仅 id+description |
| 严格执行对齐 | 3/10 | 无偏差检测，失败后继续执行下一步 |
| 动态重规划 | 0/10 | 完全缺失，失败只标记 failed 不触发 replan |
| 成本优化 | 1/10 | 无双模型、无并行、无程序化执行 |
| 可观测性 | 4/10 | 有基础追踪，但日志不结构化 |

## 二、改进目标

将 PlanExecuteLoop 从"线性步骤执行器"升级为具备以下能力的完整 Plan-and-Execute 架构：

1. **结构化规划**：步骤含依赖关系、工具提示、参数预填充、条件分支
2. **执行对齐与校验**：每步执行后校验结果，偏差时暂停或触发重规划
3. **动态重规划**：步骤失败或环境变化时自动触发 replan
4. **双模型解耦**：Planner 用强模型，Executor 用轻量模型
5. **结构化可观测性**：步骤级时间戳、执行路径追踪、结构化日志

## 三、改进方案（分四个阶段）

---

### 阶段一：结构化规划（优先级：最高）

> 目标：让 Planner 输出结构化的计划，而非纯文本步骤列表

#### 3.1.1 扩展 PlanStep 数据结构

**文件**: `agent_lib/include/agent/types.h`

```cpp
struct PlanStep {
    u8str                              id;              // 步骤ID，如 "1", "2"
    u8str                              description;     // 步骤描述
    u8str                              status;          // pending / in_progress / completed / failed / skipped
    u8str                              result;          // 步骤执行结果

    // --- 新增字段 ---
    std::vector<u8str>                 depends_on;      // 依赖的步骤ID列表
    std::optional<u8str>               tool_hint;       // 建议使用的工具名（如 "open-meteo_get_weather"）
    std::optional<u8str>               tool_args_hint;  // 预填充的工具参数（JSON字符串）
    std::optional<u8str>               expected_output;  // 预期输出描述（用于偏差检测）
    std::optional<u8str>               condition;       // 执行条件（如 "if step_2.result contains 'error'"）
    std::optional<u8str>               fallback_step;   // 失败时跳转的步骤ID
    std::chrono::system_clock::time_point start_time;   // 执行开始时间
    std::chrono::system_clock::time_point end_time;     // 执行结束时间
    int                                 retry_count{0};  // 已重试次数
};
```

#### 3.1.2 改进规划 Prompt

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp` — `plan_execute_instruction()`

当前 prompt 只要求输出 `PLAN: Step N: xxx PLAN_END`，需要改为要求 LLM 输出 JSON 格式的结构化计划：

```
=== PLANNING PHASE (IMPORTANT) ===
When given a task, you MUST first create a step-by-step plan.

Output your plan in this EXACT JSON format:
```json
{
  "summary": "Brief description of the overall plan",
  "steps": [
    {
      "id": "1",
      "description": "What to do in this step",
      "depends_on": [],
      "tool_hint": "tool_name or null",
      "tool_args_hint": {"key": "value"} or null,
      "expected_output": "What the result should look like",
      "condition": "Execution condition or null",
      "fallback_step": "Step ID to jump to on failure or null"
    }
  ]
}
```

Rules:
- depends_on: List of step IDs that must complete before this step
- tool_hint: Suggest the tool to use (helps executor choose correctly)
- tool_args_hint: Pre-fill known parameters (executor can override)
- expected_output: Describe what success looks like (used for validation)
- condition: Only execute if condition is met (e.g., "if step_2 succeeded")
- fallback_step: If this step fails, jump to this step instead of stopping
- Steps without dependencies can be executed in parallel
```

#### 3.1.3 改进 parse_plan()

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp` — `parse_plan()`

当前 `parse_plan()` 用正则逐行解析纯文本，需改为 JSON 解析 + 纯文本降级：

```cpp
Plan PlanExecuteLoop::parse_plan(const u8str& llm_output) const {
    Plan plan;
    std::string output(llm_output.begin(), llm_output.end());

    // 优先尝试 JSON 格式解析
    auto json_plan = try_parse_json_plan(output);
    if (json_plan) return *json_plan;

    // 降级：兼容旧格式 PLAN: ... PLAN_END
    return parse_text_plan(output);
}
```

其中 `try_parse_json_plan()` 使用 nlohmann::json 解析，`parse_text_plan()` 保留现有逻辑。

#### 3.1.4 构建步骤依赖图

新增依赖图工具，用于确定执行顺序和可并行步骤：

```cpp
// plan_graph.h
class PlanGraph {
public:
    explicit PlanGraph(const Plan& plan);

    // 获取当前可执行的步骤（依赖已满足 + 状态为 pending）
    std::vector<u8str> get_ready_steps() const;

    // 标记步骤完成，释放下游依赖
    void mark_completed(const u8str& step_id);

    // 标记步骤失败，根据 fallback_step 决定下一步
    std::optional<u8str> mark_failed(const u8str& step_id);

    // 检查是否存在循环依赖
    bool has_cycle() const;

    // 获取所有剩余未完成的步骤
    std::vector<u8str> get_remaining_steps() const;

private:
    struct Node {
        PlanStep                    step;
        std::vector<u8str>          dependents;   // 依赖本步骤的步骤ID
        int                         unmet_deps;   // 未满足的依赖数
    };
    std::unordered_map<u8str, Node> nodes_;
};
```

---

### 阶段二：执行对齐与动态重规划（优先级：高）

> 目标：执行偏差检测 + 失败自动重规划

#### 3.2.1 步骤结果校验

在每步执行完成后，增加结果校验逻辑：

```cpp
// plan_execute_loop.cpp — Phase 2 步骤执行后
enum class StepValidation { Pass, Fail, Uncertain };

StepValidation validate_step_result(const PlanStep& step, const u8str& result) {
    // 1. 如果步骤有 expected_output，让 LLM 判断结果是否符合预期
    if (step.expected_output.has_value() && !step.expected_output->empty()) {
        // 构造校验 prompt，让 LLM 判断
        // 返回 Pass / Fail / Uncertain
    }

    // 2. 如果工具返回 is_error，直接 Fail
    // 3. 如果结果包含错误关键词，Fail
    // 4. 默认 Pass
}
```

校验方式：
- **快速校验**（默认）：检查 `is_error` 标志 + 错误关键词匹配
- **深度校验**（步骤有 `expected_output` 时）：构造简短 prompt 让 LLM 判断结果是否符合预期

#### 3.2.2 重规划机制

当步骤失败或校验不通过时，触发重规划：

```cpp
// 重规划触发条件
enum class ReplanTrigger {
    StepFailed,          // 步骤执行失败
    ValidationFailed,    // 结果校验不通过
    EnvironmentChanged,  // 环境状态变化（如工具不可用）
};

// 重规划策略
struct ReplanConfig {
    int     max_replan_attempts{3};     // 最大重规划次数
    bool    replan_on_validation{true};  // 校验失败时是否重规划
    bool    replan_on_failure{true};     // 步骤失败时是否重规划
    bool    preserve_completed{true};    // 重规划时保留已完成步骤
};
```

重规划流程：

```
步骤失败/校验不通过
  → 检查 fallback_step：有则跳转到 fallback 步骤
  → 无 fallback：检查重规划次数 < max_replan_attempts
    → 是：构造 replan prompt（包含：原计划 + 已完成步骤结果 + 失败原因）
         → LLM 生成新计划（保留已完成步骤，调整剩余步骤）
         → 更新 PlanGraph，继续执行
    → 否：标记整体失败，输出总结
```

重规划 prompt：

```
=== RE-PLANNING ===
The original plan encountered a problem. Here is the context:

Original plan:
{original_plan_json}

Completed steps:
{completed_steps_with_results}

Failed step: Step {id} - {description}
Failure reason: {failure_reason}

Please create a revised plan. Rules:
- Keep all completed steps as-is
- Revise the remaining steps to work around the failure
- You may add new steps, remove steps, or change their order
- Output the revised plan in the same JSON format
```

#### 3.2.3 条件分支执行

利用 `condition` 和 `fallback_step` 字段实现分支：

```cpp
// 在 Phase 2 步骤执行前检查条件
bool should_execute_step(const PlanStep& step) {
    if (!step.condition.has_value()) return true;

    // 解析条件表达式，如 "if step_2.result contains 'error'"
    // 简化实现：支持以下模式
    //   "if step_N succeeded"     → 检查 step N status == completed
    //   "if step_N failed"        → 检查 step N status == failed
    //   "if step_N.result contains 'xxx'" → 检查结果文本
    return evaluate_condition(*step.condition);
}
```

---

### 阶段三：双模型解耦与成本优化（优先级：中）

> 目标：Planner 用强模型，Executor 用轻量模型，降低推理成本
>
> **核心设计原则**：Plan-and-Execute 特有的配置（planner/executor 模型、重规划策略等）
> 不应污染通用的 `AgentConfig`，而是通过模式专属的配置结构和工厂方法承载。

#### 3.3.1 拆分 Agent 工厂方法

**问题**：当前 `Agent::create(AgentMode, InnerLoopConfig, AgentConfig)` 用一个通用接口
承载所有模式，导致模式特有配置无处安放。

**方案**：拆分为模式专属的工厂方法 + 模式专属的配置结构。

**文件**: `agent_lib/include/agent/agent.h`

```cpp
class Agent {
public:
    // --- 拆分后的工厂方法 ---

    // 创建 ReAct 模式 Agent
    static AgentPtr create_react(const ReactAgentConfig& config);

    // 创建 Plan-and-Execute 模式 Agent
    static AgentPtr create_plan_execute(const PlanExecuteAgentConfig& config);

    // 创建自定义 Loop Agent（高级用法）
    static AgentPtr create_custom(AgentLoopFactory loop_factory, const AgentConfig& config);

    // --- 以下保持不变 ---
    ~Agent();
    void start();
    void stop();
    void submit_input(const u8str& input);
    // ... 状态查询、组件查询、回调 ...
};
```

**废弃**：原有的 `create(AgentMode, InnerLoopConfig, AgentConfig)` 和
`create(AgentLoopFactory, AgentConfig)` 标记为 `[[deprecated]]`，保留一个版本
以平滑迁移，后续移除。

#### 3.3.2 模式专属配置结构

**文件**: `agent_lib/include/agent/agent_config.h`

```cpp
// ========== 通用基础配置 ==========
// 所有模式共享的配置项，与模式无关
struct AgentConfig {
    // 模型配置（llm_provider 为 nullptr 时，内部据此创建内置的 llm_provider）
    LlmModelConfig                  model_config;
    // 自定义 llm_provider（nullptr 表示使用 model_config 创建）
    LlmProviderPtr                  llm_provider;
    // 组件（nullptr 表示使用内置默认实现）
    UserConfirmHandlerPtr           confirm_handler;
    PromptBuilderPtr                prompt_builder;
    ContextManagerPtr               context_manager;
    ToolRegistryPtr                 tool_registry;
    MemoryPtr                       memory;
    // 人格配置
    PersonalityDocs                 personality;
    // Skill 目录路径
    std::vector<std::filesystem::path> skill_dirs;
    // MCP 配置文件路径
    std::filesystem::path              mcp_config_path;
};

// ========== ReAct 模式专属配置 ==========
struct ReactAgentConfig : AgentConfig {
    // ReAct Loop 配置
    int     max_steps{10};
    bool    enable_thinking{true};
    bool    auto_confirm{false};
    bool    debug{false};
};

// ========== Plan-and-Execute 模式专属配置 ==========
struct PlanExecuteAgentConfig : AgentConfig {
    // PE Loop 基础配置
    int     max_steps{10};
    bool    enable_thinking{true};
    bool    auto_confirm{false};
    bool    debug{false};

    // --- PE 特有：双模型配置 ---
    // 规划用 LLM（nullptr 则使用 AgentConfig::llm_provider）
    LlmProviderPtr      planner_llm_provider;
    // 执行用 LLM（nullptr 则使用 AgentConfig::llm_provider）
    LlmProviderPtr      executor_llm_provider;
    // 规划模型配置（planner_llm_provider 为 nullptr 时，据此创建）
    LlmModelConfig      planner_model_config;
    // 执行模型配置（executor_llm_provider 为 nullptr 时，据此创建）
    LlmModelConfig      executor_model_config;

    // --- PE 特有：重规划配置 ---
    ReplanConfig        replan_config;
    int                 max_step_retries{2};
};
```

**设计要点**：
- `AgentConfig` 保持纯净，不含任何模式特有字段
- `ReactAgentConfig` / `PlanExecuteAgentConfig` 继承 `AgentConfig`，各自承载特有配置
- 双模型配置放在 `PlanExecuteAgentConfig` 中，而非 `AgentConfig`
- `planner_llm_provider` / `executor_llm_provider` 也在 `PlanExecuteAgentConfig` 中

#### 3.3.3 废弃旧接口

**文件**: `agent_lib/include/agent/agent_config.h`

```cpp
// 以下标记为废弃，保留一个版本以平滑迁移
enum class AgentMode { ReAct, PlanAndExecute };

struct InnerLoopConfig {
    int     max_steps = 10;
    bool    enable_thinking = true;
    bool    auto_confirm = false;
    bool    debug = false;
};
```

**文件**: `agent_lib/include/agent/agent.h`

```cpp
// 废弃：使用 create_react() 或 create_plan_execute() 替代
[[deprecated("use create_react() or create_plan_execute()")]]
static AgentPtr create(AgentMode mode, InnerLoopConfig loop_config, const AgentConfig& config);

// 废弃：使用 create_custom() 替代
[[deprecated("use create_custom()")]]
static AgentPtr create(AgentLoopFactory loop_factory, const AgentConfig& config);
```

#### 3.3.4 AgentLoopContext 精简

`AgentLoopContext` 是内部结构，不再承载模式特有字段：

```cpp
// AgentLoop 工厂上下文（内部使用，不暴露给外部）
struct AgentLoopContext {
    IContextManager&                context_manager;
    IPromptBuilder&                 prompt_builder;
    IToolRegistry&                  tools;
    IMcpManager&                    mcps;
    IMemory&                        memory;
    const PersonalityDocs&          personality;
    LlmProviderPtr                  llm_provider;        // 默认 LLM
    UserConfirmHandlerPtr           confirm_handler;
};
```

模式特有 Provider 通过工厂 lambda 闭包捕获，而非放入 `AgentLoopContext`：

```cpp
// agent_impl.cpp 中 PlanExecute 的工厂创建
Agent::Impl::Impl(const PlanExecuteAgentConfig& config)
{
    init_components(config);

    // 捕获 PE 特有的 Provider
    auto planner_llm = planner_llm_;   // 在 init_components 中创建
    auto executor_llm = executor_llm_; // 在 init_components 中创建

    agent_loop_factory_ = [config_capture = PlanExecuteLoopConfig{...}]
        (const AgentLoopContext& ctx) -> std::unique_ptr<IAgentLoop> {
        return std::make_unique<PlanExecuteLoop>(
            ctx.llm_provider, ctx.confirm_handler,
            ctx.context_manager, ctx.prompt_builder,
            ctx.tools, ctx.mcps, ctx.memory,
            ctx.personality, config_capture,
            planner_llm, executor_llm  // 额外参数
        );
    };
}
```

#### 3.3.5 PlanExecuteLoop 构造函数扩展

```cpp
class PlanExecuteLoop : public AgentLoopBase {
public:
    PlanExecuteLoop(LlmProviderPtr llm_provider,
                    UserConfirmHandlerPtr confirm_handler,
                    IContextManager& context,
                    IPromptBuilder& prompt_builder,
                    IToolRegistry& tools,
                    IMcpManager& mcps,
                    IMemory& memory,
                    const PersonalityDocs& personality,
                    PlanExecuteLoopConfig config,
                    LlmProviderPtr planner_llm = nullptr,   // 新增：规划专用
                    LlmProviderPtr executor_llm = nullptr);  // 新增：执行专用

private:
    LlmProviderPtr   planner_llm_;    // 规划专用（nullptr 则用 llm_provider_）
    LlmProviderPtr   executor_llm_;   // 执行专用（nullptr 则用 llm_provider_）

    LlmResponse call_llm_for_plan(const LlmRequest& request);
    LlmResponse call_llm_for_execute(const LlmRequest& request);
};
```

#### 3.3.6 init_components 双 Provider 创建

```cpp
void Agent::Impl::init_components(const PlanExecuteAgentConfig& config) {
    // 通用组件初始化（同现有逻辑）
    init_components(static_cast<const AgentConfig&>(config));

    // PE 特有：创建 planner LLM
    if (config.planner_llm_provider) {
        planner_llm_ = config.planner_llm_provider;
    } else if (config.planner_model_config.model_type != LlmModelType::Custom
               || !config.planner_model_config.api_key.empty()) {
        // planner_model_config 有实质内容时才创建
        planner_llm_ = LlmProviderFactory::create(config.planner_model_config);
    } else {
        planner_llm_ = llm_provider_;  // 回退到默认
    }

    // PE 特有：创建 executor LLM（同理）
    if (config.executor_llm_provider) {
        executor_llm_ = config.executor_llm_provider;
    } else if (config.executor_model_config.model_type != LlmModelType::Custom
               || !config.executor_model_config.api_key.empty()) {
        executor_llm_ = LlmProviderFactory::create(config.executor_model_config);
    } else {
        executor_llm_ = llm_provider_;  // 回退到默认
    }
}
```

#### 3.3.7 步骤并行执行

利用 `PlanGraph::get_ready_steps()` 获取当前无依赖冲突的可执行步骤，并行提交：

```cpp
// Phase 2 执行循环改进
while (graph.has_remaining_steps()) {
    auto ready_steps = graph.get_ready_steps();

    if (ready_steps.empty()) {
        // 死锁：有剩余步骤但无就绪步骤 → 依赖无法满足
        break;
    }

    // 并行执行就绪步骤
    if (ready_steps.size() == 1) {
        // 单步骤：直接执行（避免线程开销）
        execute_plan_step(ready_steps[0]);
    } else {
        // 多步骤：并行执行
        std::vector<std::future<StepResult>> futures;
        for (const auto& step_id : ready_steps) {
            futures.push_back(std::async(std::launch::async, [this, &step_id] {
                return execute_plan_step(step_id);
            }));
        }
        // 收集结果
        for (size_t i = 0; i < futures.size(); ++i) {
            auto result = futures[i].get();
            if (result.success) {
                graph.mark_completed(ready_steps[i]);
            } else {
                auto fallback = graph.mark_failed(ready_steps[i]);
                // 处理 fallback 或触发 replan
            }
        }
    }
}
```

**注意**：并行执行需要为每个步骤创建独立的上下文分支，避免消息交叉。这要求 `IContextManager` 支持快照/分支能力（见阶段四）。

---

### 阶段四：接口层适配与可观测性（优先级：中）

> 目标：确保 IAgentLoop 接口覆盖所有模式的正常运转，Agent::Impl 适配不同 Loop，
> 并提供结构化可观测性

#### 3.4.1 IAgentLoop 接口审查与改造

**问题**：当前 `IAgentLoop` 的 `needs_user_input()` 在 `AgentLoopBase` 中统一实现为
文本模式匹配（检查"请输入/请确认"等关键词），对 PE 模式语义不准确——PE 暂停等待
用户确认时，输出文本可能不含这些关键词，但确实需要用户介入。

**方案**：`needs_user_input()` 从纯虚函数改为有默认实现的虚函数，由各 Loop 子类
自行决定语义。

**文件**: `agent_lib/include/agent/i_agent_loop.h`

```cpp
class IAgentLoop {
public:
    virtual ~IAgentLoop() = default;

    // ===== 生命周期 =====
    virtual void run(const u8str& user_input) = 0;
    virtual void interrupt(const u8str& new_input) = 0;
    virtual void stop() = 0;

    // ===== 状态查询 =====
    virtual AgentState get_state() const = 0;
    virtual std::vector<ThinkingStep> get_thinking_steps() const = 0;
    virtual std::optional<u8str> get_final_output() const = 0;
    virtual std::optional<Plan> get_plan() const = 0;

    // ===== 回调 =====
    virtual void set_on_thinking_update(std::function<void(const ThinkingStep&)> callback) = 0;
    virtual void set_on_output_ready(std::function<void(const u8str&)> callback) = 0;
    virtual void set_on_state_change(std::function<void(AgentState)> callback) = 0;

    // ===== 自动继续 =====
    virtual bool should_auto_continue() const { return false; }

    // ===== 用户输入需求（各 Loop 自行决定语义） =====
    // ReAct: 检查输出文本中的"请输入/请确认"等模式
    // PE: 检查 AgentState::WaitingUserConfirm 状态或步骤暂停标志
    virtual bool needs_user_input() const { return false; }

    // ===== 执行日志（仅 PE 有意义，ReAct 返回 nullopt） =====
    virtual std::optional<PlanExecutionLog> get_execution_log() const { return std::nullopt; }
};
```

**各 Loop 的 `needs_user_input()` 实现**：

```cpp
// ReactLoop：保持原有文本模式匹配逻辑
bool ReactLoop::needs_user_input() const {
    auto output = get_final_output();
    if (!output) return false;
    return u8str_util::needs_user_input(*output);
}

// PlanExecuteLoop：基于状态判断
bool PlanExecuteLoop::needs_user_input() const {
    // 1. WaitingUserConfirm 状态 = 明确需要用户
    if (get_state() == AgentState::WaitingUserConfirm) return true;
    // 2. 步骤暂停等待输入
    if (paused_for_user_input_) return true;
    // 3. 也检查输出文本模式（兼容）
    auto output = get_final_output();
    if (output && u8str_util::needs_user_input(*output)) return true;
    return false;
}
```

**接口覆盖性审查**：

| 方法 | ReAct | PE（改进后） | 覆盖？ |
|------|-------|-------------|--------|
| `run(input)` | ✅ | ✅ | 是 |
| `interrupt(input)` | ✅ | ✅ | 是 |
| `stop()` | ✅ | ✅ | 是 |
| `get_state()` | ✅ | ✅ | 是 |
| `get_thinking_steps()` | ✅ | ✅ | 是 |
| `get_final_output()` | ✅ | ✅ | 是 |
| `get_plan()` | 返回 nullopt | ✅ | 是 |
| `set_on_*` (3个) | ✅ | ✅ | 是 |
| `should_auto_continue()` | ✅ | ✅ | 是 |
| `needs_user_input()` | ✅ 文本匹配 | ✅ 状态+文本 | 是（改造后） |
| `get_execution_log()` | 返回 nullopt | ✅ | 是（新增） |

#### 3.4.2 AgentLoopBase 适配

`AgentLoopBase` 移除 `needs_user_input()` 的统一实现，改为由子类各自重写：

**文件**: `agent_lib/src/agent/agent_loop_base.h`

```cpp
class AgentLoopBase : public IAgentLoop {
public:
    // ... 现有公共方法 ...

    // 移除 needs_user_input() 的统一实现，由子类重写

protected:
    // ... 现有 protected 成员 ...
};
```

**文件**: `agent_lib/src/agent/agent_loop_base.cpp`

```cpp
// 删除 AgentLoopBase::needs_user_input() 实现
```

#### 3.4.3 Agent::Impl 改造

**问题**：当前 `Impl` 只有一个 `llm_provider_`，PE 模式需要的 `planner_llm_`/`executor_llm_`
没有存放位置。且旧构造函数 `Impl(AgentMode, InnerLoopConfig, AgentConfig)` 无法承载
模式特有配置。

**方案**：新增模式专属构造函数，PE 特有 Provider 作为 Impl 成员，通过工厂 lambda
闭包捕获传递给 Loop。

**文件**: `agent_lib/src/agent/agent_impl.h`

```cpp
class Agent::Impl {
public:
    // --- 新增：模式专属构造函数 ---
    Impl(const ReactAgentConfig& config);
    Impl(const PlanExecuteAgentConfig& config);
    Impl(AgentLoopFactory loop_factory, const AgentConfig& config);

    // --- 废弃：旧构造函数（内部委托到新构造函数） ---
    [[deprecated]] Impl(AgentMode mode, InnerLoopConfig loop_config, const AgentConfig& config);

    ~Impl();

    // ... 其余公共方法不变 ...

private:
    void process_loop();
    void init_components(const AgentConfig& config);  // 通用组件初始化

    // --- 通用组件 ---
    ContextManagerPtr                   context_;
    PromptBuilderPtr                    prompt_builder_;
    ToolRegistryPtr                     tools_;
    McpManagerPtr                       mcps_;
    MemoryPtr                           memory_;
    PersonalityDocs                     personality_docs_;
    LlmProviderPtr                      llm_provider_;        // 默认 LLM
    UserConfirmHandlerPtr               confirm_handler_;
    AgentLoopFactory                    agent_loop_factory_;

    // --- PE 模式特有组件（仅 PlanExecute 构造函数填充） ---
    LlmProviderPtr                      planner_llm_;         // 规划 LLM
    LlmProviderPtr                      executor_llm_;        // 执行 LLM

    std::unique_ptr<IAgentLoop>         current_loop_;

    std::atomic<bool>                   running_{false};
    std::atomic<bool>                   processing_{false};
    std::queue<u8str>                   input_queue_;
    mutable std::mutex                  queue_mutex_;
    std::condition_variable             queue_cv_;
    std::thread                         worker_thread_;

    std::function<void(const ThinkingStep&)>   on_thinking_update_;
    std::function<void(const u8str&)>          on_output_ready_;
    std::function<void(AgentState)>            on_state_change_;
    mutable std::mutex                         callback_mutex_;
};
```

**文件**: `agent_lib/src/agent/agent_impl.cpp` — 构造函数实现

```cpp
// ========== ReAct 构造函数 ==========
Agent::Impl::Impl(const ReactAgentConfig& config) {
    init_components(config);

    // ReAct 工厂：直接使用默认 LLM
    agent_loop_factory_ = [config](const AgentLoopContext& ctx)
        -> std::unique_ptr<IAgentLoop> {
        return std::make_unique<ReactLoop>(
            ctx.llm_provider, ctx.confirm_handler,
            ctx.context_manager, ctx.prompt_builder,
            ctx.tools, ctx.mcps, ctx.memory,
            ctx.personality, ReactLoopConfig{config}
        );
    };
}

// ========== PE 构造函数 ==========
Agent::Impl::Impl(const PlanExecuteAgentConfig& config) {
    init_components(config);

    // PE 特有：创建 planner LLM
    if (config.planner_llm_provider) {
        planner_llm_ = config.planner_llm_provider;
    } else if (has_real_config(config.planner_model_config)) {
        planner_llm_ = LlmProviderFactory::create(config.planner_model_config);
    } else {
        planner_llm_ = llm_provider_;
    }

    // PE 特有：创建 executor LLM
    if (config.executor_llm_provider) {
        executor_llm_ = config.executor_llm_provider;
    } else if (has_real_config(config.executor_model_config)) {
        executor_llm_ = LlmProviderFactory::create(config.executor_model_config);
    } else {
        executor_llm_ = llm_provider_;
    }

    // PE 工厂：闭包捕获 planner/executor LLM
    auto captured_planner = planner_llm_;
    auto captured_executor = executor_llm_;
    agent_loop_factory_ = [config, captured_planner, captured_executor]
        (const AgentLoopContext& ctx) -> std::unique_ptr<IAgentLoop> {
        return std::make_unique<PlanExecuteLoop>(
            ctx.llm_provider, ctx.confirm_handler,
            ctx.context_manager, ctx.prompt_builder,
            ctx.tools, ctx.mcps, ctx.memory,
            ctx.personality, PlanExecuteLoopConfig{config},
            captured_planner, captured_executor
        );
    };
}

// ========== 自定义 Loop 构造函数 ==========
Agent::Impl::Impl(AgentLoopFactory loop_factory, const AgentConfig& config) {
    init_components(config);
    agent_loop_factory_ = std::move(loop_factory);
}

// ========== 废弃：旧构造函数（委托到新构造函数） ==========
[[deprecated]] Agent::Impl::Impl(AgentMode mode, InnerLoopConfig loop_config,
                                  const AgentConfig& config) {
    init_components(config);

    if (mode == AgentMode::PlanAndExecute) {
        PlanExecuteAgentConfig pe_config;
        static_cast<AgentConfig&>(pe_config) = config;
        pe_config.max_steps = loop_config.max_steps;
        pe_config.enable_thinking = loop_config.enable_thinking;
        pe_config.auto_confirm = loop_config.auto_confirm;
        pe_config.debug = loop_config.debug;
        // 重新走 PE 构造逻辑
        planner_llm_ = llm_provider_;
        executor_llm_ = llm_provider_;
        agent_loop_factory_ = [loop_config](const AgentLoopContext& ctx)
            -> std::unique_ptr<IAgentLoop> {
            return std::make_unique<PlanExecuteLoop>(
                ctx.llm_provider, ctx.confirm_handler,
                ctx.context_manager, ctx.prompt_builder,
                ctx.tools, ctx.mcps, ctx.memory,
                ctx.personality, PlanExecuteLoopConfig{loop_config}
            );
        };
    } else {
        agent_loop_factory_ = [loop_config](const AgentLoopContext& ctx)
            -> std::unique_ptr<IAgentLoop> {
            return std::make_unique<ReactLoop>(
                ctx.llm_provider, ctx.confirm_handler,
                ctx.context_manager, ctx.prompt_builder,
                ctx.tools, ctx.mcps, ctx.memory,
                ctx.personality, ReactLoopConfig{loop_config}
            );
        };
    }
}
```

**关键设计**：
- `process_loop()` 本身不需要改动——它只调用 `agent_loop_factory_(ctx)`，不关心内部创建了什么 Loop
- 模式特有 Provider（`planner_llm_`/`executor_llm_`）是 Impl 的成员，通过 lambda 闭包捕获传递
- `AgentLoopContext` 保持精简，不承载模式特有字段

#### 3.4.4 Agent 层模式特有查询方法

**设计决策**：是否需要拆分 `ReactAgent` / `PlanExecuteAgent` 两个类？

**结论：不需要**。Agent 的全部职责是纯基础设施（线程、队列、回调、组件生命周期），
所有模式差异 100% 在 Loop 内部。拆分会产生大量重复代码（start/stop/submit_input/
process_loop/set_on_* 全部一样），违反 DRY。

**方案**：Agent 保持统一，模式特有查询通过 `std::optional` 方法暴露。
`std::optional` 天然表达"可能没有"的语义——ReAct 返回 `nullopt`，PE 返回实际值。
这与已有的 `get_plan()` 模式一致。

**不采用暴露 Loop 的方案**，原因：
- `current_loop_` 每轮 `process_loop()` 重建，暴露指针随时失效
- 暴露内部实现细节，破坏封装

**文件**: `agent_lib/include/agent/agent.h`

```cpp
class Agent {
public:
    // 工厂方法
    static AgentPtr create_react(const ReactAgentConfig& config);
    static AgentPtr create_plan_execute(const PlanExecuteAgentConfig& config);
    static AgentPtr create_custom(AgentLoopFactory loop_factory, const AgentConfig& config);

    // 废弃：旧工厂方法
    [[deprecated("use create_react() or create_plan_execute()")]]
    static AgentPtr create(AgentMode mode, InnerLoopConfig loop_config, const AgentConfig& config);
    [[deprecated("use create_custom()")]]
    static AgentPtr create(AgentLoopFactory loop_factory, const AgentConfig& config);

    ~Agent();

    // ===== 通用 API（所有模式共享） =====
    void start();
    void stop();
    void submit_input(const u8str& input);

    // 状态查询
    std::vector<ThinkingStep> get_thinking() const;
    std::optional<u8str>      get_output() const;
    ContextSnapshot           get_context() const;
    AgentState                get_state() const;

    // 组件查询
    LlmProviderPtr              get_llm_provider() const;
    UserConfirmHandlerPtr       get_confirm_handler() const;
    PromptBuilderPtr            get_prompt_builder() const;
    ContextManagerPtr           get_context_manager() const;
    ToolRegistryPtr             get_tool_registry() const;
    McpManagerPtr               get_mcp_manager() const;
    MemoryPtr                   get_memory() const;
    const PersonalityDocs&      get_personality() const;

    // 回调
    void set_on_thinking_update(std::function<void(const ThinkingStep&)> callback);
    void set_on_output_ready(std::function<void(const u8str&)> callback);
    void set_on_state_change(std::function<void(AgentState)> callback);

    // ===== 模式相关查询（ReAct 返回 nullopt，PE 返回实际值） =====

    // 获取计划（已有，PE 返回 Plan，ReAct 返回 nullopt）
    std::optional<Plan> get_plan() const;

    // 获取执行日志（新增，PE 返回 PlanExecutionLog，ReAct 返回 nullopt）
    std::optional<PlanExecutionLog> get_execution_log() const;

private:
    Agent() = default;
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};
```

**Impl 层实现**（委托到 Loop）：

```cpp
std::optional<Plan> Agent::Impl::get_plan() const {
    if (current_loop_) {
        return current_loop_->get_plan();
    }
    return std::nullopt;
}

std::optional<PlanExecutionLog> Agent::Impl::get_execution_log() const {
    if (current_loop_) {
        return current_loop_->get_execution_log();
    }
    return std::nullopt;
}
```

**使用示例**：

```cpp
// ReAct 用户：只用通用 API
auto agent = Agent::create_react(react_config);
agent->start();
agent->submit_input(u8str(u8"查天气"));
// get_plan() → nullopt
// get_execution_log() → nullopt

// PE 用户：通用 API + 模式相关查询
auto agent = Agent::create_plan_execute(pe_config);
agent->start();
agent->submit_input(u8str(u8"查天气并制定出行建议"));
// get_plan() → Plan{...}
// get_execution_log() → PlanExecutionLog{...}
auto log = agent->get_execution_log();
if (log) {
    // 输出结构化摘要
    print_summary(*log);
}
```

**方案对比**：

| 方案 | 封装性 | 类型安全 | 用户代码 | 代码重复 |
|------|--------|---------|---------|---------|
| 暴露 `get_loop()` | 差 | 需 downcast | `dynamic_cast<PELoop*>(agent->get_loop())->...` | 无 |
| Agent 上加 `std::optional` 方法 ✅ | 好 | 好 | `agent->get_execution_log()` | 无 |
| 拆分 ReactAgent / PlanExecuteAgent | 好 | 最好 | 两个类 | 大量重复 |

#### 3.4.5 扩展 ThinkingStep

```cpp
struct ThinkingStep {
    int                                    step_index;
    u8str                                  thinking_content;
    std::optional<ToolCall>                tool_call;
    std::optional<ToolResult>              tool_result;
    std::chrono::system_clock::time_point  timestamp;

    // --- 新增 ---
    u8str                                  plan_step_id;       // 关联的计划步骤ID
    u8str                                  phase;              // "planning" / "execution" / "replanning" / "validation"
    std::optional<int>                     duration_ms;        // 本步耗时（毫秒）
};
```

#### 3.4.6 执行日志结构

新增 `PlanExecutionLog`，记录完整的计划执行过程：

```cpp
struct PlanStepLog {
    u8str                                  step_id;
    u8str                                  description;
    u8str                                  status;             // completed / failed / skipped
    std::chrono::system_clock::time_point  start_time;
    std::chrono::system_clock::time_point  end_time;
    int                                    duration_ms;
    std::optional<ToolCall>                tool_call;
    std::optional<ToolResult>              tool_result;
    StepValidation                         validation_result;
    int                                    retry_count;
    std::optional<u8str>                   replan_reason;      // 如果触发了重规划
};

struct PlanExecutionLog {
    u8str                                  original_plan;      // 原始计划 JSON
    std::vector<PlanStepLog>               step_logs;
    int                                    replan_count{0};
    std::chrono::system_clock::time_point  plan_start_time;
    std::chrono::system_clock::time_point  plan_end_time;
    int                                    total_duration_ms;
    u8str                                  final_status;       // "completed" / "partial" / "failed"
};
```

#### 3.4.7 CLI 结构化摘要输出

CLI 层可在任务完成后输出结构化摘要：

```
========== Plan Execution Summary ==========
Plan: 查询姜堰天气并制定出行建议
Total: 4 steps | 3 completed | 0 failed | 1 skipped
Duration: 12.3s
Replans: 0

Step 1 [completed] 查询天气数据        2.1s  open-meteo_get_weather
Step 2 [completed] 查询空气质量         1.8s  open-meteo_get_air_quality
Step 3 [completed] 综合分析并生成建议    5.2s  (LLM reasoning)
Step 4 [skipped]   发送邮件通知          -     (condition not met)
============================================
```

---

## 四、实施路径

### Phase 1 — 结构化规划（建议首先实施）

| 任务 | 涉及文件 | 改动量 |
|------|----------|--------|
| 扩展 PlanStep 结构 | `types.h` | 小 |
| 改进规划 Prompt | `plan_execute_loop.cpp` | 中 |
| 改进 parse_plan() 支持 JSON | `plan_execute_loop.cpp` | 中 |
| 新增 PlanGraph 依赖图 | 新增 `plan_graph.h/.cpp` | 中 |
| Phase 2 使用 PlanGraph 驱动执行 | `plan_execute_loop.cpp` | 大 |

### Phase 2 — 执行对齐与重规划

| 任务 | 涉及文件 | 改动量 |
|------|----------|--------|
| 新增 ReplanConfig | `agent_config.h` | 小 |
| 新增步骤结果校验 | `plan_execute_loop.cpp` | 中 |
| 新增重规划流程 | `plan_execute_loop.cpp` | 大 |
| 新增条件分支执行 | `plan_execute_loop.cpp` | 中 |

### Phase 3 — 双模型与成本优化

| 任务 | 涉及文件 | 改动量 |
|------|----------|--------|
| 新增 ReactAgentConfig / PlanExecuteAgentConfig | `agent_config.h` | 中 |
| 拆分 Agent::create → create_react/create_plan_execute/create_custom | `agent.h`, `agent_impl.h/.cpp` | 中 |
| 旧接口标记 [[deprecated]] | `agent.h`, `agent_config.h` | 小 |
| AgentLoopContext 精简（移除模式特有字段） | `agent_config.h` | 小 |
| PlanExecuteLoop 构造函数扩展（planner/executor llm 参数） | `plan_execute_loop.h/.cpp` | 中 |
| init_components 双 Provider 创建 | `agent_impl.cpp` | 中 |
| CLI 迁移到新工厂方法 | `agent_cli/main.cpp` | 小 |
| 测试迁移到新工厂方法 | `tests/framework_test/main.cpp` | 小 |
| 步骤并行执行 | `plan_execute_loop.cpp` | 大 |

### Phase 4 — 接口层适配与可观测性

| 任务 | 涉及文件 | 改动量 |
|------|----------|--------|
| IAgentLoop 接口改造（needs_user_input 下放 + get_execution_log） | `i_agent_loop.h` | 小 |
| AgentLoopBase 移除 needs_user_input 统一实现 | `agent_loop_base.h/.cpp` | 小 |
| ReactLoop 重写 needs_user_input | `react_loop.h/.cpp` | 小 |
| PlanExecuteLoop 重写 needs_user_input | `plan_execute_loop.h/.cpp` | 小 |
| Agent::Impl 新增模式专属构造函数 | `agent_impl.h/.cpp` | 中 |
| Agent::Impl 旧构造函数标记 deprecated | `agent_impl.h/.cpp` | 小 |
| Agent 新增 get_execution_log() 方法 | `agent.h`, `agent_impl.cpp` | 小 |
| 扩展 ThinkingStep | `types.h` | 小 |
| 新增 PlanExecutionLog / PlanStepLog | `types.h` | 小 |
| PlanExecuteLoop 记录执行日志 | `plan_execute_loop.cpp` | 中 |
| CLI 输出结构化摘要 | `agent_cli/` | 小 |

---

## 五、兼容性策略

1. **向后兼容**：`parse_plan()` 同时支持 JSON 和纯文本格式，旧格式自动降级
2. **渐进式采用**：PlanStep/ThinkingStep 新增字段全部 `std::optional`，不填则行为与当前一致
3. **双模型可选**：`PlanExecuteAgentConfig` 中 `planner_llm_provider` / `executor_llm_provider` 为 `nullptr` 时回退到 `AgentConfig::llm_provider`
4. **并行可选**：无依赖步骤默认仍串行执行，需显式配置启用并行
5. **重规划可控**：`ReplanConfig` 可配置 `max_replan_attempts=0` 禁用重规划
6. **旧接口平滑迁移**：`Agent::create(AgentMode, ...)` 标记 `[[deprecated]]` 但仍可用，内部委托到新工厂方法
7. **配置继承兼容**：`ReactAgentConfig` / `PlanExecuteAgentConfig` 继承 `AgentConfig`，现有代码传 `AgentConfig` 的地方无需改动
8. **needs_user_input 下放兼容**：`IAgentLoop::needs_user_input()` 有默认实现（返回 false），`ReactLoop` 保持原有文本匹配语义，现有 ReAct 行为不变
9. **Agent 不拆分**：Agent 保持统一类，模式特有查询通过 `std::optional` 方法暴露（`get_plan()`/`get_execution_log()`），ReAct 返回 `nullopt`，PE 返回实际值，不暴露内部 Loop

## 六、风险与注意事项

1. **JSON 解析鲁棒性**：LLM 输出的 JSON 可能格式不规范，需要宽松解析 + 降级策略
2. **并行执行的上下文隔离**：当前 `IContextManager` 不支持分支，并行执行时需要为每个步骤创建独立上下文副本
3. **重规划的上下文膨胀**：每次重规划都会增加对话历史，需配合 `compress()` 控制长度
4. **条件表达式安全性**：`condition` 字段的表达式解析需要限制范围，避免注入风险
5. **LLM 校验的额外成本**：深度校验需要额外 LLM 调用，应仅在 `expected_output` 存在时启用
