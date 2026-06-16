# Plan-and-Execute 改进实现计划

> 基于 spec.md 的改进方案，按代码依赖关系重新排序实施步骤
> 每个步骤完成后必须编译通过 + 测试通过

---

## Step 1：扩展数据结构（无行为变更，纯加字段）

**目标**：为后续所有改动准备数据结构，不改变任何运行时行为

### 1.1 扩展 PlanStep

**文件**: `agent_lib/include/agent/types.h`

新增字段（全部 `std::optional` 或有默认值，不填则行为与当前一致）：
- `std::vector<u8str> depends_on` — 依赖步骤ID（默认空）
- `std::optional<u8str> tool_hint` — 建议工具名
- `std::optional<u8str> tool_args_hint` — 预填充参数 JSON
- `std::optional<u8str> expected_output` — 预期输出描述
- `std::optional<u8str> condition` — 执行条件
- `std::optional<u8str> fallback_step` — 失败跳转步骤ID
- `std::chrono::system_clock::time_point start_time` — 执行开始时间
- `std::chrono::system_clock::time_point end_time` — 执行结束时间
- `int retry_count{0}` — 已重试次数

### 1.2 扩展 ThinkingStep

**文件**: `agent_lib/include/agent/types.h`

新增字段：
- `u8str plan_step_id` — 关联计划步骤ID（默认空）
- `u8str phase` — "planning"/"execution"/"replanning"/"validation"（默认空）
- `std::optional<int> duration_ms` — 本步耗时

### 1.3 新增 PlanExecutionLog / PlanStepLog

**文件**: `agent_lib/include/agent/types.h`

新增结构体：
- `PlanStepLog` — 步骤执行日志
- `PlanExecutionLog` — 整体执行日志

### 1.4 新增 ReplanConfig

**文件**: `agent_lib/include/agent/agent_config.h`

```cpp
struct ReplanConfig {
    int  max_replan_attempts{3};
    bool replan_on_validation{true};
    bool replan_on_failure{true};
    bool preserve_completed{true};
};
```

### 1.5 新增 StepValidation 枚举

**文件**: `agent_lib/include/agent/types.h`

```cpp
enum class StepValidation { Pass, Fail, Uncertain };
```

### 1.6 新增 ReactAgentConfig / PlanExecuteAgentConfig

**文件**: `agent_lib/include/agent/agent_config.h`

- `ReactAgentConfig : AgentConfig` — 含 max_steps/enable_thinking/auto_confirm/debug
- `PlanExecuteAgentConfig : AgentConfig` — 含基础配置 + 双模型 + ReplanConfig + max_step_retries

### 1.7 新增 ReactLoopConfig / PlanExecuteLoopConfig

**文件**: `agent_lib/include/agent/agent_config.h`

替代现有 `InnerLoopConfig`，各自从对应的 AgentConfig 构造。

**验证**：编译通过，现有测试通过

---

## Step 2：IAgentLoop 接口改造

**目标**：`needs_user_input()` 下放 + 新增 `get_execution_log()`

### 2.1 IAgentLoop 接口变更

**文件**: `agent_lib/include/agent/i_agent_loop.h`

- `needs_user_input()` 改为有默认实现（返回 false）
- 新增 `virtual std::optional<PlanExecutionLog> get_execution_log() const { return std::nullopt; }`

### 2.2 AgentLoopBase 移除统一实现

**文件**: `agent_lib/src/agent/agent_loop_base.h/.cpp`

- 删除 `AgentLoopBase::needs_user_input()` 实现

### 2.3 ReactLoop 重写 needs_user_input

**文件**: `agent_lib/src/agent/react_loop.h/.cpp`

- 保持原有文本模式匹配逻辑

### 2.4 PlanExecuteLoop 重写 needs_user_input

**文件**: `agent_lib/src/agent/plan_execute_loop.h/.cpp`

- 检查 `WaitingUserConfirm` 状态 + `paused_for_user_input_` + 文本模式匹配

### 2.5 Agent 新增 get_execution_log()

**文件**: `agent_lib/include/agent/agent.h`, `agent_lib/src/agent/agent_impl.cpp`

- `Agent::get_execution_log()` → 委托到 `current_loop_->get_execution_log()`

**验证**：编译通过，现有测试通过

---

## Step 3：Agent 工厂方法拆分

**目标**：拆分 `create()` 为 `create_react()`/`create_plan_execute()`/`create_custom()`

### 3.1 Agent 类新增工厂方法

**文件**: `agent_lib/include/agent/agent.h`

```cpp
static AgentPtr create_react(const ReactAgentConfig& config);
static AgentPtr create_plan_execute(const PlanExecuteAgentConfig& config);
static AgentPtr create_custom(AgentLoopFactory loop_factory, const AgentConfig& config);
```

### 3.2 旧工厂方法标记 deprecated

**文件**: `agent_lib/include/agent/agent.h`

```cpp
[[deprecated("use create_react() or create_plan_execute()")]]
static AgentPtr create(AgentMode mode, InnerLoopConfig loop_config, const AgentConfig& config);
[[deprecated("use create_custom()")]]
static AgentPtr create(AgentLoopFactory loop_factory, const AgentConfig& config);
```

### 3.3 Agent::Impl 新增模式专属构造函数

**文件**: `agent_lib/src/agent/agent_impl.h/.cpp`

- `Impl(const ReactAgentConfig& config)` — 创建 ReactLoop 工厂
- `Impl(const PlanExecuteAgentConfig& config)` — 创建 PE 工厂 + planner/executor LLM
- `Impl(AgentLoopFactory, const AgentConfig&)` — 自定义工厂
- 旧构造函数标记 deprecated，内部委托到新构造函数

### 3.4 Agent::Impl 新增 planner_llm_ / executor_llm_ 成员

**文件**: `agent_lib/src/agent/agent_impl.h`

### 3.5 CLI 迁移到新工厂方法

**文件**: `agent_cli/main.cpp`

### 3.6 测试迁移到新工厂方法

**文件**: `tests/framework_test/main.cpp`

**验证**：编译通过，现有测试通过，CLI 正常运行

---

## Step 4：PlanExecuteLoop 双模型支持

**目标**：Planner/Executor 使用不同 LLM

### 4.1 PlanExecuteLoop 构造函数扩展

**文件**: `agent_lib/src/agent/plan_execute_loop.h/.cpp`

- 新增 `LlmProviderPtr planner_llm = nullptr` / `LlmProviderPtr executor_llm = nullptr` 参数
- 新增 `planner_llm_` / `executor_llm_` 成员（nullptr 回退到 `llm_provider_`）

### 4.2 call_llm_for_plan / call_llm_for_execute

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp`

- `call_llm_for_plan()` 使用 `planner_llm_`
- `call_llm_for_execute()` 使用 `executor_llm_`
- 替换现有 `call_llm()` 调用点

**验证**：编译通过，PE 模式单模型/双模型均可正常运行

---

## Step 5：结构化规划

**目标**：Planner 输出 JSON 格式计划，支持步骤依赖/工具提示/参数预填充

### 5.1 改进规划 Prompt

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp` — `plan_execute_instruction()`

- 要求 LLM 输出 JSON 格式计划（含 depends_on/tool_hint/tool_args_hint/expected_output/condition/fallback_step）
- 保留纯文本格式说明作为降级

### 5.2 改进 parse_plan()

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp`

- 新增 `try_parse_json_plan()` — JSON 格式解析
- 保留 `parse_text_plan()` — 纯文本降级
- `parse_plan()` 优先 JSON，失败降级纯文本

### 5.3 新增 PlanGraph 依赖图

**文件**: 新增 `agent_lib/src/agent/plan_graph.h/.cpp`

- `PlanGraph(const Plan& plan)` — 从 Plan 构建依赖图
- `get_ready_steps()` — 获取当前可执行步骤
- `mark_completed(step_id)` — 标记完成，释放下游
- `mark_failed(step_id)` → `optional<fallback_step>` — 标记失败
- `has_cycle()` — 循环依赖检测
- `get_remaining_steps()` — 剩余步骤

### 5.4 Phase 2 使用 PlanGraph 驱动执行

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp`

- 替换现有线性 `for (i = 0; i < steps.size(); i++)` 循环
- 改为 `while (graph.has_remaining_steps())` + `graph.get_ready_steps()`
- 步骤执行时填充 `tool_hint`/`tool_args_hint` 到 executor prompt
- 步骤执行前后记录 `start_time`/`end_time`

**验证**：编译通过，PE 模式可生成 JSON 计划并按依赖顺序执行，纯文本计划降级正常

---

## Step 6：执行对齐与重规划

**目标**：步骤结果校验 + 失败自动重规划 + 条件分支

### 6.1 步骤结果校验

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp`

- 新增 `validate_step_result()` — 快速校验（is_error + 错误关键词）+ 深度校验（LLM 判断 expected_output）
- 步骤执行完成后调用校验

### 6.2 重规划流程

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp`

- 步骤失败/校验不通过 → 检查 fallback_step → 检查 replan 次数
- 新增 `replan()` 方法 — 构造 replan prompt，调用 planner_llm_，更新 PlanGraph
- replan prompt 包含：原计划 + 已完成步骤结果 + 失败原因
- 重规划时保留已完成步骤，调整剩余步骤

### 6.3 条件分支执行

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp`

- 新增 `should_execute_step()` — 解析 condition 表达式
- 支持模式：`if step_N succeeded` / `if step_N failed` / `if step_N.result contains 'xxx'`
- 条件不满足时标记 `skipped`

### 6.4 步骤重试

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp`

- 步骤失败且 retry_count < max_step_retries 时，重试当前步骤
- 重试 ≠ 重规划（重试是同一步骤再执行一次，重规划是生成新计划）

**验证**：编译通过，步骤失败可触发重规划，条件分支可跳过步骤，重试可恢复临时错误

---

## Step 7：结构化可观测性

**目标**：执行日志记录 + CLI 结构化摘要

### 7.1 PlanExecuteLoop 记录执行日志

**文件**: `agent_lib/src/agent/plan_execute_loop.h/.cpp`

- 新增 `execution_log_` 成员（`PlanExecutionLog`）
- 步骤执行时记录 `PlanStepLog`
- 重规划时记录 `replan_reason`
- 实现 `get_execution_log()` 返回 `execution_log_`

### 7.2 CLI 输出结构化摘要

**文件**: `agent_cli/main.cpp`

- 任务完成后，调用 `agent->get_execution_log()`
- 如果有日志，输出结构化摘要表格

**验证**：编译通过，PE 任务完成后输出结构化摘要

---

## Step 8：步骤并行执行（可选，优先级最低）

**目标**：无依赖步骤并行执行

### 8.1 并行执行框架

**文件**: `agent_lib/src/agent/plan_execute_loop.cpp`

- `get_ready_steps()` 返回多个步骤时，使用 `std::async` 并行执行
- 每个步骤需要独立的上下文副本（需 `IContextManager` 支持快照）

### 8.2 IContextManager 快照支持

**文件**: `agent_lib/include/agent/i_context_manager.h`, `agent_lib/src/agent/context_manager.h/.cpp`

- 新增 `create_snapshot()` / `restore_snapshot()` 方法

**验证**：编译通过，无依赖步骤可并行执行，结果正确

---

## 实施依赖关系

```
Step 1 (数据结构) ──→ Step 2 (IAgentLoop) ──→ Step 3 (工厂方法拆分)
                                                       │
                                                       ├──→ Step 4 (双模型)
                                                       │
Step 1 (数据结构) ──→ Step 5 (结构化规划) ──→ Step 6 (重规划)
                                                       │
Step 5 + Step 6 ──→ Step 7 (可观测性)
                                                       │
Step 5 ──→ Step 8 (并行执行，可选)
```

**关键路径**：Step 1 → Step 5 → Step 6 → Step 7

**可并行**：Step 2/3/4 可与 Step 5/6 并行开发

## 每步完成标准

1. 编译通过（`build.ps1`）
2. 现有测试通过（`framework_test.exe` + `tool_test.exe`）
3. 新增功能有对应测试用例
4. 代码符合项目编码规范（C++20、命名规范、成员变量对齐）
