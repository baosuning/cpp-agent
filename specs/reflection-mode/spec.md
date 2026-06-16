# Reflection 模式 Spec

> **Status: 已实现** (2026-06-14)
> 
> 所有需求已实现并通过测试。实现细节参见:
> - `agent_lib/src/agent/reflection_loop.h/cpp` — ReflectionLoop 核心循环
> - `agent_lib/include/agent/agent_config.h` — ReflectionAgentConfig
> - `agent_cli/main.cpp` — CLI 集成
> - `tests/framework_test/main.cpp` — 6 项 Reflection 测试

## Why

当前 Agent Framework 支持 ReAct 和 Plan-and-Execute 两种运行模式，但两者都是"一次性"输出：ReAct 在工具调用完成后直接输出答案，Plan-Execute 按计划执行后汇总输出。两者都缺乏**对输出质量的自我审视和改进**能力。

Reflection 模式填补这一空白：Agent 生成初始输出后，通过一个显式的 Critic（批评者）角色审视输出质量，发现问题后自动改进，循环迭代直到质量达标。这在代码生成、复杂推理、长文写作等质量敏感型场景中能显著提升最终输出质量。

## What Changes

- **新增 `AgentMode::Reflection`** 枚举值
- **新增 `ReflectionAgentConfig`** 配置结构体（继承 AgentConfig），支持单模型/双模型模式
- **新增 `ReflectionLoopConfig`** 内部配置（从 ReflectionAgentConfig 构造）
- **新增 `ReflectionLoop`** 类（继承 AgentLoopBase），实现 Generate → Critique → Refine 循环
- **新增 `Agent::create_reflection()`** 工厂方法
- **agent_cli 支持 `--mode reflection`** 命令行参数和 `agent_mode: reflection` 配置文件选项
- **新增 `reflection_instruction()`**：生成器系统指令
- **新增 `critique_instruction()`**：批评者系统指令
- **新增 `refine_instruction()`**：改进器系统指令

## Impact

- Affected specs: 无（新增模式，不影响已有模式）
- Affected code: `agent_config.h`, `agent.h`, `agent_impl.h`, `agent_impl.cpp`, `react_loop.h`/`.cpp`（未使用，但需参考其模式）, `main.cpp`, 新建 `reflection_loop.h`/`.cpp`

---

## ADDED Requirements

### Requirement: Reflection 模式枚举

系统 SHALL 在 `AgentMode` 枚举中新增 `Reflection` 值，用于标识 Reflection 运行模式。

#### Scenario: 模式枚举包含 Reflection
- **GIVEN** `AgentMode` 枚举定义
- **WHEN** 检查枚举值
- **THEN** 包含 `ReAct`, `PlanAndExecute`, `Reflection` 三个值

---

### Requirement: ReflectionAgentConfig 配置

系统 SHALL 提供 `ReflectionAgentConfig` 结构体，继承自 `AgentConfig`，包含 Reflection 模式专属配置项。

#### Scenario: 基础配置继承
- **GIVEN** `ReflectionAgentConfig` 实例
- **WHEN** 设置 `max_steps=15`, `enable_thinking=true`, `auto_confirm=false`, `debug=false`
- **THEN** 这些值被正确存储，与 ReactAgentConfig 的对应字段行为一致

#### Scenario: 反射专属配置
- **GIVEN** `ReflectionAgentConfig` 实例
- **WHEN** 设置 `max_reflection_rounds=3`
- **THEN** 该值被正确存储，默认值为 3

#### Scenario: 单模型模式（默认）
- **GIVEN** `ReflectionAgentConfig` 实例
- **WHEN** `critic_llm_provider` 和 `critic_model_config.api_key` 均为空
- **THEN** Critic 使用与 Generator 相同的 LLM（仅 Prompt 不同）

#### Scenario: 双模型模式（Critic 独立配置）
- **GIVEN** `ReflectionAgentConfig` 实例
- **WHEN** 提供独立的 `critic_llm_provider`
- **THEN** Critic 使用该独立 LLM 进行评估

---

### Requirement: ReflectionLoop 循环引擎

系统 SHALL 实现 `ReflectionLoop` 类，继承 `AgentLoopBase`，实现 Generate → Critique → Refine 循环。

#### Scenario: 创建 ReflectionLoop 实例
- **GIVEN** 有效的 LLM Provider、Context、Tools 等依赖
- **WHEN** 构造 `ReflectionLoop` 并传入 `ReflectionLoopConfig`
- **THEN** 实例创建成功，无异常

#### Scenario: 生成阶段（Phase 1: Generate）
- **GIVEN** 用户输入 "写一段 Python 快速排序代码"
- **WHEN** ReflectionLoop 启动
- **THEN** 
  - 构建 system_prompt（personality + reflection_instruction）
  - 调用 LLM（带 tools）生成初始回答
  - 如果 LLM 调用了工具（如读取文件），正确执行工具并将结果注入上下文
  - 初始回答（含文本版本）存储为 `current_answer`

#### Scenario: 反思阶段（Phase 2: Critique）
- **GIVEN** 已有一个初始回答
- **WHEN** 进入反思阶段
- **THEN**
  - 调用 Critic LLM（带 critique_instruction），传入原始用户请求和当前回答
  - Critic 返回结构化评价：score(1-10)、issues（问题列表）、suggestions（改进建议）、acceptable(YES/NO)
  - 如果 acceptable=YES，结束循环，将当前回答作为最终输出

#### Scenario: 改进阶段（Phase 2: Refine）
- **GIVEN** Critique 返回 acceptable=NO
- **WHEN** 进入改进阶段
- **THEN**
  - 调用 Generator LLM（带 refine_instruction），传入原始请求、当前回答、批评反馈
  - Generator 可能调用工具辅助改进（如执行代码验证正确性）
  - 生成改进后的回答，替换 `current_answer`
  - 回到 Critique 阶段

#### Scenario: 达到最大反射轮次
- **GIVEN** `max_reflection_rounds=2`
- **WHEN** 两轮反射后 Critique 仍返回 acceptable=NO
- **THEN** 循环终止，输出当前最佳回答，并附加提示"已达到最大反思次数，以下是当前最优结果"

#### Scenario: 最终阶段（Phase 3: Output）
- **GIVEN** 反射循环结束（无论通过还是达到上限）
- **WHEN** 输出最终结果
- **THEN**
  - 调用 `emit_output(final_answer)` 输出内容
  - 调用 `set_state(Completed)` 标记完成
  - 将反思历史存储到 Memory（供后续任务复用）

#### Scenario: 工具调用的确认处理
- **GIVEN** 生成或改进阶段 LLM 调用需要用户确认的工具
- **WHEN** `auto_confirm` 为 false
- **THEN** 触发确认流程，等待用户确认后再执行

#### Scenario: debug 模式
- **GIVEN** `debug=true`
- **WHEN** ReflectionLoop 运行
- **THEN** 输出 Critic 的完整评价内容和每轮改进的详细日志

---

### Requirement: Prompt 指令

系统 SHALL 提供三条专用的 Prompt 指令：

- `reflection_instruction()`：生成器的系统指令，告知 LLM 当前处于 Reflection 模式
- `critique_instruction(user_query, current_answer)`：批评者的 Prompt，引导 LLM 输出结构化评价
- `refine_instruction(user_query, current_answer, critique)`：改进器的 Prompt，引导 LLM 根据反馈改进输出

#### Scenario: reflection_instruction 内容
- **WHEN** 构建生成器系统指令
- **THEN** 指令包含：
  - 模式说明：你是一个采用 Reflection 模式的 AI Agent
  - 核心流程：Generate → Critique → Refine 循环
  - 工具可用：如果任务需要外部信息，使用工具获取
  - 输出要求：生成全面、准确的回答

#### Scenario: critique_instruction 内容
- **WHEN** 构建批评者 Prompt
- **THEN** 指令包含：
  - 角色设定：你是一名严格的评审员
  - 评估标准：正确性、完整性、清晰度、可操作性
  - 输出格式：JSON 结构化评价
  - 包含 score(1-10)、issues(数组)、suggestions(数组)、acceptable("YES"/"NO")

#### Scenario: refine_instruction 内容
- **WHEN** 构建改进器 Prompt
- **THEN** 指令包含：
  - 原始用户请求
  - 上一轮回答（标注为"需要改进"）
  - 批评者反馈（具体问题和建议）
  - 改进要求：逐一解决所有问题，可以使用工具辅助改进

---

### Requirement: Agent 工厂方法

系统 SHALL 提供 `Agent::create_reflection()` 静态工厂方法。

#### Scenario: 创建 Reflection Agent
- **GIVEN** 一个有效的 `ReflectionAgentConfig`
- **WHEN** 调用 `Agent::create_reflection(config)`
- **THEN** 返回非空的 `AgentPtr`，Agent 内部使用 ReflectionLoop

#### Scenario: agent_cli 模式选择
- **GIVEN** `agent.md` 中 `agent_mode: reflection`
- **WHEN** agent_cli 启动
- **THEN** 创建 Reflection 模式的 Agent

#### Scenario: 命令行覆盖
- **GIVEN** 命令行参数 `--mode reflection`
- **WHEN** agent_cli 启动
- **THEN** 无论配置文件如何设置，创建 Reflection 模式的 Agent

---

### Requirement: Thinking Steps 和状态报告

系统 SHALL 在 Reflection 各阶段输出 thinking steps，供 UI 展示进度。

#### Scenario: thinking steps 包含反射进度
- **WHEN** ReflectionLoop 运行
- **THEN** thinking steps 包含：
  - `[Reflection] Generating initial answer...`
  - `[Reflection] Round 1/3: Critiquing...`
  - `[Reflection] Round 1/3: Critique result - score=7/10, acceptable=NO`
  - `[Reflection] Round 1/3: Refining answer...`
  - `[Reflection] Round 2/3: Critiquing...`
  - `[Reflection] Answer accepted (score=9/10)`

#### Scenario: 反射结果存储到 Memory
- **GIVEN** Reflection 循环完成
- **WHEN** 输出最终结果
- **THEN** 将 key 为 `reflection_history` 的结构化反思记录存储到 Memory

---

### Requirement: 配置文档更新

系统 SHALL 更新 `README.md` 中的配置说明，添加 `agent_mode: reflection` 和 `max_reflection_rounds` 的文档。

#### Scenario: README 包含 reflection 配置说明
- **WHEN** 查看 README.md 的配置表格
- **THEN** 包含 `agent_mode` 的可选值 `reflection` 说明，以及 `max_reflection_rounds` 配置项说明