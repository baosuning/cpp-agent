# Checklist: Reflection 模式

## 配置与枚举

- [x] `AgentMode::Reflection` 枚举值已添加到 `agent_config.h`
- [x] `ReflectionAgentConfig` 结构体定义正确，继承自 `AgentConfig`
- [x] `ReflectionAgentConfig` 包含 `max_reflection_rounds`（默认 3）
- [x] `ReflectionAgentConfig` 包含 `critic_llm_provider` 和 `critic_model_config`
- [x] `ReflectionLoopConfig` 结构体定义正确，支持从 `ReflectionAgentConfig` 构造

## Agent 工厂方法

- [x] `agent.h` 中声明了 `Agent::create_reflection()`
- [x] `agent_impl.cpp` 中实现了 `Agent::Impl::Impl(const ReflectionAgentConfig&)`
- [x] 单模型模式：Critic 不独立配置时复用主 LLM
- [x] 双模型模式：独立 `critic_llm_provider` 或 `critic_model_config` 时正确创建独立 Critic LLM
- [x] `Agent::create_reflection()` 返回非空的 `AgentPtr`

## ReflectionLoop 核心逻辑

- [x] `reflection_loop.h` 声明 `ReflectionLoop : public AgentLoopBase`
- [x] 构造函数参数正确（llm_provider, confirm_handler, context, prompt_builder, tools, mcps, memory, personality, config, critic_llm）
- [x] Phase 1（Generate）：构建 system_prompt + 调用 LLM（带工具），生成初始回答
- [x] Phase 2（Critique）：调用 Critic LLM 评价当前回答，返回结构化 JSON（score/issues/suggestions/acceptable）
- [x] Phase 2（Refine）：acceptable=NO 时，调用 Generator LLM 生成改进版本
- [x] Phase 2（循环）：Critique → Refine 循环最多执行 `max_reflection_rounds` 次
- [x] Phase 2（终止）：acceptable=YES 或达到最大轮次时退出循环
- [x] Phase 3（Output）：调用 `emit_output()` 输出最终结果，调用 `set_state(Completed)`
- [x] 工具调用：生成和改进阶段 LLM 可正常调用工具
- [x] 确认处理：工具需要确认时正确触发确认流程
- [x] 错误处理：LLM 调用失败时输出错误并设置 Error 状态

## Prompt 指令

- [x] `reflection_instruction()` 描述 Reflection 模式的核心流程
- [x] `critique_instruction()` 要求 Critic 输出结构化 JSON 评价
- [x] `refine_instruction()` 包含原始请求、当前回答、批评反馈、改进要求
- [x] Critic JSON 解析（`parse_critique_response()`）容错处理（格式不完美时降级处理）

## Thinking Steps 与 Memory

- [x] 各阶段输出 thinking steps：生成中、反思中（第 N 轮）、改进中、接受/拒绝
- [x] 最终结果存储到 Memory（reflection_history）

## agent_cli 集成

- [x] `agent_mode: reflection` 在 agent.md 配置文件中生效
- [x] `--mode reflection` 命令行参数生效
- [x] `max_reflection_rounds` 配置项从 agent.md 读取（默认 3）

## 测试

- [x] 测试 Mock LLM：Critic 返回 acceptable=YES → 直接输出
- [x] 测试 Mock LLM：Critic 先 NO 后 YES → 改进后输出
- [x] 测试 Mock LLM：Critic 始终 NO → 达到最大轮次后输出
- [x] 测试工具调用在生成阶段正常工作
- [x] 测试 thinking steps 包含反射进度
- [x] 测试状态转换和工厂方法

## 文档

- [x] README.md 配置表格包含 `reflection` 模式说明
- [x] README.md 包含 `max_reflection_rounds` 配置项

## 编译与回归

- [x] `build.ps1` 编译通过，无警告
- [x] `framework_test.exe` 全部通过（88/88，包括新增 6 个测试 + 原有 82 个测试）
- [x] `tool_test.exe` 全部通过（23/23）
- [x] 已有 ReAct 和 Plan-and-Execute 模式功能不受影响