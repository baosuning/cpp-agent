# Tasks: Reflection 模式

- [x] Task 1: 新增 AgentMode 枚举和配置结构体
  - [x] 在 `agent_config.h` 的 `AgentMode` 枚举中新增 `Reflection` 值
  - [x] 新增 `ReflectionAgentConfig` 结构体（继承 `AgentConfig`）：包含 `max_steps`、`enable_thinking`、`auto_confirm`、`debug`、`max_reflection_rounds`、`critic_llm_provider`、`critic_model_config`
  - [x] 新增 `ReflectionLoopConfig` 结构体（内部使用，从 `ReflectionAgentConfig` 构造）

- [x] Task 2: 新增 Agent 工厂方法声明
  - [x] 在 `agent.h` 中新增 `static AgentPtr create_reflection(const ReflectionAgentConfig& config)` 声明

- [x] Task 3: 实现 Agent::create_reflection 工厂方法
  - [x] 在 `agent_impl.cpp` 中新增 `Agent::Impl::Impl(const ReflectionAgentConfig& config)` 构造函数
  - [x] 处理 Critic LLM 的创建逻辑（独立配置则创建独立实例，否则复用主 LLM）
  - [x] 工厂闭包中创建 `ReflectionLoop` 实例
  - [x] 实现 `Agent::create_reflection()` 静态方法

- [x] Task 4: 实现 ReflectionLoop 类
  - [x] 新建 `reflection_loop.h`：声明 `ReflectionLoop` 类，继承 `AgentLoopBase`
  - [x] 新建 `reflection_loop.cpp`：实现核心循环逻辑
  - [x] 实现 `run()` 方法：三阶段流程（Generate → Critique/Refine → Output）
  - [x] 实现 `reflection_instruction()`：生成器系统指令
  - [x] 实现 `critique_instruction()`：批评者 Prompt，要求结构化 JSON 输出
  - [x] 实现 `refine_instruction()`：改进器 Prompt
  - [x] 实现 `parse_critique_response()`：解析 Critic 的结构化 JSON 评价
  - [x] 实现 `should_auto_continue()` 和 `needs_user_input()` 虚方法
  - [x] 实现 `get_plan()` 返回 nullopt（Reflection 模式无计划）

- [x] Task 5: agent_cli 模式选择支持
  - [x] 在 `main.cpp` 的 `mode_str` 分支中添加 `"reflection"` 支持
  - [x] 命令行 `--mode reflection` 参数解析
  - [x] 读取 `max_reflection_rounds` 配置项（默认 3）
  - [x] 创建 `ReflectionAgentConfig` 并调用 `Agent::create_reflection()`

- [x] Task 6: 更新 README 配置文档
  - [x] 在 README.md 的配置表格中添加 `agent_mode: reflection` 说明
  - [x] 添加 `max_reflection_rounds` 配置项说明

- [x] Task 7: 编写单元测试
  - [x] 测试 ReflectionLoop 基本生成 + 接受流程（Mock LLM：Critic 返回 acceptable=YES）
  - [x] 测试 ReflectionLoop 生成 + 拒绝 + 改进 + 接受流程（Mock LLM：Critic 先返回 NO，再返回 YES）
  - [x] 测试达到最大反射轮次上限（Mock LLM：Critic 始终返回 NO）
  - [x] 测试工具调用在生成阶段正常工作
  - [x] 测试 thinking steps 包含反射进度信息
  - [x] 测试状态转换和工厂方法

- [x] Task 8: 编译验证
  - [x] 使用 `build.ps1` 编译通过
  - [x] 运行 `framework_test.exe` 全部通过（88/88）
  - [x] 运行 `tool_test.exe` 全部通过（23/23）

# Task Dependencies

- Task 2 依赖 Task 1（需要配置结构体定义）
- Task 3 依赖 Task 1 和 Task 2（需要配置 + 声明）
- Task 4 依赖 Task 1（需要 ReflectionLoopConfig）
- Task 5 依赖 Task 1、Task 3、Task 4（需要完整实现）
- Task 7 依赖 Task 3、Task 4（需要 ReflectionLoop 实现）
- Task 8 依赖 Task 1-7（全部完成后编译验证）
- Task 6 可与 Task 1-7 并行