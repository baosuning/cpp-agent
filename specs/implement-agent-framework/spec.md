# Agent Framework 设计规格说明

> **Status: 已实现** (2026-06)
>
> 本文档描述 Agent Framework 的整体设计规格。具体模块的详细设计参见:
> - `.trae/specs/architecture/spec.md` — 架构设计文档
> - `.trae/specs/reflection-mode/spec.md` — Reflection 模式设计
> - `.trae/specs/plan-execute-improvement/spec.md` — Plan-and-Execute 改进方案
> - `.trae/specs/refactor-agent-lib-decoupling/spec.md` — 架构解耦重构

## 概述

一个轻量级、C++20 原生的 AI Agent 框架库，支持 ReAct、Plan-and-Execute 和 Reflection 三种 Agent 模式。提供模块化、接口清晰、可扩展的架构，使开发者能够快速构建基于大模型的智能体应用。

## 架构概览

### 分层架构

```
┌──────────────────────────────────────────────────────────┐
│                    Agent CLI                              │
│  (main.cpp, web_search, skills, mcps, tools, memory)     │
├──────────────────────────────────────────────────────────┤
│              Agent 公共接口层 (include/agent/)             │
│  agent.h, types.h, agent_config.h, personality.h         │
│  i_tool.h (ITool + IToolRegistry), i_mcp.h (IMcpManager) │
│  i_llm_provider.h, i_memory.h, i_agent_loop.h            │
│  i_prompt_builder.h, i_context_manager.h                 │
│  i_user_confirm_handler.h, builtin_tools.h               │
├──────────────────────────────────────────────────────────┤
│                    Agent 核心实现 (src/)                    │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ │
│  │ Agent  │ │ ReAct  │ │PlanExec│ │Reflect │ │ Prompt │ │
│  │ Impl   │ │ Loop   │ │ Loop   │ │ Loop   │ │Builder │ │
│  └────────┘ └────────┘ └────────┘ └────────┘ └────────┘ │
│  ┌────────┐                                              │
│  │Context │                                              │
│  │Manager │                                              │
│  └────────┘                                              │
├──────────────────────────────────────────────────────────┤
│              模块管理器 / 注册表                            │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ │
│  │  Tool  │ │  MCP   │ │ Memory │ │ Person │ │  Skill │ │
│  │Registry│ │Manager │ │ (IMem) │ │ Docs   │ │Scanner │ │
│  └────────┘ └────────┘ └────────┘ └────────┘ └────────┘ │
├──────────────────────────────────────────────────────────┤
│               LLM Provider 实现                            │
│   OpenAI │ Claude │ Kimi │ DeepSeek │ GLM                │
├──────────────────────────────────────────────────────────┤
│             MCP / 内置工具                                 │
│  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐ │
│  │  MCP   │ │   FS   │ │   Web  │ │  Cmd   │ │ Script │ │
│  │ Client │ │  Tools │ │  Fetch │ │  Tool  │ │  Tools │ │
│  └────────┘ └────────┘ └────────┘ └────────┘ └────────┘ │
├──────────────────────────────────────────────────────────┤
│                 基础设施                                    │
│  nlohmann/json.hpp │ WinHTTP (Windows) │ JSON-RPC 2.0    │
└──────────────────────────────────────────────────────────┘
```

### 数据流

1. 用户输入 → `Agent::submit_input()` → 输入队列
2. Worker 线程轮询队列 → `process_loop()`
3. 根据工厂方法创建对应的循环引擎实例（`IAgentLoop`）
4. 循环引擎执行：
   - 构建系统 Prompt（SOUL + IDENTITY + AGENTS + Tool/MCP Schema + Skill 列表 + 模式指令）
   - 调用 LLM
   - 解析响应，执行工具调用
   - 循环直到 LLM 返回无 tool_calls 的最终输出
5. 上下文管理 `compress()` 滑动窗口压缩
6. **Auto-Continue 判断**：检查最终输出是否在请求用户输入
   - 需要用户输入 → 停下来等待
   - 不需要 → 自动提交"继续"，回到步骤 4
7. 输出结果到回调函数

## 核心组件

### 1. Agent 主类

`Agent` 类对外暴露纯净接口，通过工厂方法创建 `std::shared_ptr<Agent>`：

- `Agent::create_react(const ReactAgentConfig&)` — 创建 ReAct 模式 Agent
- `Agent::create_plan_execute(const PlanExecuteAgentConfig&)` — 创建 Plan-and-Execute 模式 Agent
- `Agent::create_reflection(const ReflectionAgentConfig&)` — 创建 Reflection 模式 Agent
- `Agent::create_custom(AgentLoopFactory, const AgentConfig&)` — 自定义 Loop（高级用法）

关键接口：
- `start()` / `stop()` — 生命周期管理
- `submit_input(u8str)` / `submit_input_interrupt(u8str)` — 输入提交（可中断当前思考）
- `register_tool(ToolPtr)` / `update_tool()` / `remove_tool()` / `list_tools()` — 工具管理
- `get_tool_registry()` → `shared_ptr<IToolRegistry>` — 获取工具注册表接口
- `get_mcp_manager()` → `shared_ptr<IMcpManager>` — 获取 MCP 管理器接口
- `set_memory(MemoryPtr)` / `get_memory()` / `remove_memory()` — 记忆管理
- `set_on_thinking_update(callback)` / `set_on_output_ready(callback)` / `set_on_state_change(callback)` — 回调注册
- `get_thinking()` / `get_output()` / `get_context()` / `get_state()` — 状态获取
- `get_plan()` → `optional<Plan>` — 获取执行计划（PE 模式返回 Plan，其他模式返回 nullopt）
- `get_execution_log()` → `optional<PlanExecutionLog>` — 获取执行日志（PE 模式返回日志，其他模式返回 nullopt）

内部实现 `Agent::Impl` 组合所有子模块，通过工作线程 + 条件变量队列处理输入。

### 2. ReAct 循环

`ReactLoop` 实现 `IAgentLoop` 接口，是 Agent 的核心推理引擎之一。

**循环流程**：
1. 接收用户输入 → `ContextManager.add_user_message()`
2. 检索相关记忆 → 注入上下文
3. 构建系统 Prompt（SOUL + IDENTITY + AGENTS + 工具schema + MCP + 技能 + OS + 日期 + ReAct 指令）
4. 调用 `ILlmProvider.send_request()` 获取 LLM 响应
5. 解析响应：
   - 包含 `tool_calls` → 查找执行 → 反馈结果 → 回到步骤 4
   - 不包含 `tool_calls` → 视为本轮结果
6. 超出 `max_steps` → 使用最后一次内容或工具结果
7. ReactLoop 返回后，进入 **Auto-Continue 判断**（在 `process_loop()` 中）：
   - LLM 输出包含"请输入"、"?"、"需要你"等用户输入模式 → 停止，等待用户
   - Agent 处于 `WaitingUserConfirm` 状态 → 停止，等待用户
   - 否则 → 自动提交"继续"，回到步骤 1（新的 ReactLoop 实例）

**工具查找顺序**（两级）：`ToolRegistry.get_tool(name)` → `McpManager` MCP 工具名解析

**MCP 工具名解析**（支持三种格式）：
1. `mcp__{server}__{tool}` — 双下划线格式
2. `{server}_{tool}` — 下划线格式（如 `obscura_browser_navigate`）
3. `{server}.{tool}` — 点号格式

**浏览器工具特殊处理**：`is_browser_launch_tool()` 检测浏览器启动类工具（`cloak_launch`、`browser_navigate`），自动强制设置 `headless=false`。

**安全性**：
- `contains_dangerous_keywords()` 检测危险命令
- `needs_confirmation()` 判断工具是否需要确认
- 工具 `requires_confirmation() == true` 触发确认回调

### 3. Plan-and-Execute 循环

`PlanExecuteLoop` 实现 `IAgentLoop` 接口。

**Phase 1: Planning（规划阶段）**：
1. 接收用户输入，搜索相关记忆
2. 调用 LLM 生成多步执行计划
3. 解析计划文本为 `Plan` 结构（支持 `Step N:` / `- xxx` / `N. xxx` 等格式）
4. 如果 LLM 同时返回 `tool_calls`，也会执行
5. 如果计划为空（简单问题），直接返回 LLM 回答

**Phase 2: Execute（执行阶段）**：
1. 逐步执行每个 `PlanStep`
2. 对每个步骤：构建 step_prompt → 调用 `execute_single_step()`
3. `execute_single_step()` 内部也是循环：调用 LLM → 执行工具 → 继续
4. 支持**文本格式工具调用解析**（`parse_text_tool_calls()`，兼容 GLM 等模型）
5. 如果 LLM 只返回文本不调用工具，最多重试 3 次
6. 如果步骤输出需要用户输入，设置 `WaitingUserConfirm` 状态并暂停

**Phase 3: Summarize（汇总阶段）**：
1. 所有步骤完成后调用 LLM 综合各步骤结果
2. 禁止工具调用，仅做文本合成
3. 低温度 (0.3) 保持输出一致性

**步骤状态跟踪**：`pending` / `in_progress` / `completed` / `failed` / `skipped`

**双模型支持**：Planner 和 Executor 可使用独立 LLM（`planner_*` / `executor_*` 配置），未配置时共享主 LLM。

**与 ReAct 的核心差异**：

| 维度 | ReAct | Plan-and-Execute |
|------|-------|------------------|
| 循环结构 | 单循环 | 三阶段（规划+执行+汇总） |
| 计划 | 无 | 有，`get_plan()` 返回 `Plan` 结构 |
| 步骤跟踪 | 无 | 有（pending/in_progress/completed/failed） |
| LLM 调用次数 | 较少 | 较多（规划+每步执行） |
| 适用场景 | 简单任务 | 复杂多步骤任务 |
| 文本工具调用解析 | 不支持 | 支持（兼容 GLM） |
| 工具调用重试 | 无 | 最多 3 次强制工具调用 |

### 4. Reflection 循环

`ReflectionLoop` 实现 `IAgentLoop` 接口，通过 Generate → Critique → Refine 循环提升输出质量。

**Phase 1: Generate（生成阶段）**：
1. 构建 Reflection 专用 system prompt
2. 调用 LLM（带工具）生成初始回答
3. 保存 Phase 1 结束时的消息计数，用于后续上下文截断优化

**Phase 2: Critique + Refine（反思改进阶段）**：
1. Critic LLM 评估当前回答质量，输出结构化评价（score 1-10、issues、suggestions、acceptable）
2. 如果 acceptable=YES → 输出当前回答
3. 如果 acceptable=NO → 每轮 Refine 前截断上下文到 Phase 1 状态
4. Generator LLM 根据批评反馈改进回答
5. 重复直到通过或达到 `max_reflection_rounds`

**双模型支持**：Critic 可使用独立 LLM（`critic_*` 配置），未配置时共享主 LLM。

**Critic 降级策略**：Critic LLM 失败时，先尝试 Generator self-critique，两次都失败才最终降级为接受当前回答。

**上下文截断优化**：每轮 Refine 前调用 `truncate_to_messages()` 回退上下文到 Phase 1 状态，避免多轮 Refine 导致 token 浪费。

**三模式对比**：

| 维度 | ReAct | Plan-and-Execute | Reflection |
|------|-------|------------------|------------|
| 循环结构 | 单循环（推理↔行动交替） | 三阶段（规划→执行→汇总） | Generate → Critique → Refine |
| 计划生成 | 无 | LLM 生成结构化计划 | 无 |
| 步骤跟踪 | 无 | 有（状态机 + 依赖管理） | 无 |
| LLM 调用次数 | 较少 | 较多（规划 + 每步执行 + 汇总） | 较多（生成 + 多轮反思） |
| 工具调用解析 | 仅原生 function calling | 支持文本格式解析（兼容 GLM） | 仅原生 function calling |
| 适合场景 | 简单问答、单步工具 | 复杂多步骤、浏览器自动化 | 代码生成、复杂推理、质量敏感型 |
| 质量自检 | 无 | 无 | 有（Critic 评分 + Refine 改进） |

### 5. Prompt 构建器

`PromptBuilder` 实现 `IPromptBuilder` 接口，负责组装 LLM 消息。

**系统 Prompt 组成**：
- Soul (人格描述)
- Identity (身份信息)
- Principles (行为规范 / AGENTS)
- Tools (可用工具 schema 列表)
- Skills (可用技能列表)
- MCP Services (MCP 服务描述)
- System Environment (OS 信息、当前日期)
- 模式相关指令 (ReAct / Plan-Execute / Reflection)

**用户 Prompt 组成**：
- User Profile (目标用户认知)
- User Input (用户原始输入)

**OS 信息获取**（Windows）：
- 使用 `GetConsoleOutputCP()` 获取 codepage
- 使用注册表 `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion` 获取版本信息
- 避免使用 `_popen`/`_pclose`，防止 Windows 管道错误

### 6. 上下文管理器

`DefaultContextManager` 实现 `IContextManager` 接口。

**方法**：
- `add_system_message(u8str)` / `add_user_message(u8str)` / `add_assistant_message(...)` / `add_tool_message(u8str, u8str)`
- `get_messages()` — 返回消息列表
- `get_snapshot()` — 返回包含消息+状态+思考步骤+最终输出的快照
- `message_count()` / `clear()` / `compress()`
- `truncate_to_messages(size_t)` — 截断上下文到指定消息数（Reflection 模式用）

**角色**：System, User, Assistant, Tool

**压缩策略**：当消息数超过 `max_messages_` 时，前 1/3 消息摘要为一条 System 消息，保留后 2/3。

线程安全：`std::shared_mutex` 读写锁。

### 7. LLM Provider 系统

**接口**：`ILlmProvider` 定义 `send_request()` 和 `send_request_async()`。

**工厂**：`LlmProviderFactory::create()` 根据 `LlmModelType` 创建 Provider。

| Provider | 请求格式 | 特殊处理 |
|----------|---------|---------|
| OpenAI | Chat Completions API | 标准格式 |
| Claude | Anthropic Messages API | claude-3 消息格式，支持 thinking 块 |
| Kimi | Moonshot API | OpenAI 兼容格式 |
| DeepSeek | Chat Completions API | 支持 `deepseek-thinking` thinking mode |
| GLM | OpenAI 兼容 API | 支持 `reasoning_content` 思考链 |

**HTTP 客户端**：`WinHttpClient` (Windows, TLS 1.2+1.3) / `StubHttpClient` (Linux)

### 8. 工具系统

**接口**：`ITool` 位于 `include/agent/i_tool.h`。

**纯虚方法**：
- `name()` / `description()` / `parameters_schema()` — 元数据
- `execute(u8str)` — 同步执行，返回 JSON 结果字符串
- `execute_async(u8str, callback)` — 异步执行
- `requires_confirmation()` — 是否需用户确认

**注册表接口**：`IToolRegistry` 位于 `include/agent/i_tool.h`，线程安全，支持 `register_tool()` / `update_tool()` / `remove_tool()` / `get_tool()` / `list_tools()` / `get_tools_schema()`。

**内置工具**：

| 工具类 | name() | 说明 |
|--------|--------|------|
| ReadFileTool | read_file | 读取文件内容 |
| WriteFileTool | write_file | 写入文件 |
| ListDirectoryTool | list_directory | 列出目录 |
| CreateDirectoryTool | create_directory | 创建目录 |
| SearchFileTool | search_file | 搜索文件 |
| DeletePathTool | delete_path | 删除 (需确认) |
| RenamePathTool | rename_path | 重命名/移动 (需确认) |
| WebFetchTool | web_fetch | 抓取网页 HTML→Text |
| ExecuteCommandTool | execute_command | 执行命令 (含危险命令检测) |
| PythonScriptTool | python_script | 执行 Python 脚本 |
| ShellScriptTool | run_script | 执行 Batch/Shell 脚本 |
| ReadSkillTool | read_skill | 读取扫描到的技能 |

**搜索工具**（按环境变量条件加载）：

| 工具类 | name() | 环境变量 |
|--------|--------|----------|
| BingSearchTool | bing_search | `BING_SEARCH_KEY` |
| BochaSearchTool | bocha_search | `BOCHA_SEARCH_KEY` |
| VolcanoSearchTool | volcano_search | `VOLCANO_SEARCH_KEY` |
| OpenSERPSearchTool | openserp_search | `OPENSERP_SEARCH_ENGINES` |
| BaiduAiSearchTool | baidu_ai_search | `BAIDU_AI_SEARCH_KEY` |

**CLI 工具**（agent_cli 层）：

| 工具类 | name() | 说明 |
|--------|--------|------|
| EchoTool | echo | 回声工具 |
| QrCodeTool | qr_code | 二维码生成 (含控制台打印) |

### 9. MCP 系统

**协议**：JSON-RPC 2.0（命名空间 `agent::jsonrpc` 中的纯函数）。

**接口**：
- `IMcpClient` — MCP 客户端抽象接口（`name()`、`call()`、`list_tools()`），位于 `include/agent/i_mcp.h`
- `IMcpManager` — MCP 管理器抽象接口（`register_mcp()`、`get_mcp()`、`list_mcps()`、`has_mcp()`），位于 `include/agent/i_mcp.h`

**传输层**：
- `StdioTransport` — 子进程 stdin/stdout 通信，使用 `CreatePipe` + `CreateProcessA`
- `HttpTransport` — HTTP POST 请求通信

**当前内置 MCP Server**：

| MCP Server | 传输方式 | 说明 |
|------------|----------|------|
| CloakBrowser | Stdio | 基于 Chromium 的浏览器自动化 |
| Obscura | Stdio (本地进程) | Rust 轻量级无头浏览器，12 个工具 |
| 高德地图 | HTTP | 地理编码、路线规划、POI 搜索 |

**管理器**：`McpManager` 线程安全（`std::shared_mutex`），支持 `register_mcp()` / `get_mcp()` / `list_mcps()`。

**MCP 配置解析**：由 agent_cli 负责读取 `mcp.json`，创建 McpClient 实例，通过 `agent->get_mcp_manager()->register_mcp()` 注册。

### 10. 技能系统

**扫描器**：`SkillScanner` 扫描 `config/skills/*/SKILL.md`。

**技能格式**：YAML Front Matter 定义 name + description，正文包含执行逻辑。

**工具集成**：`ReadSkillTool` 将扫描到的技能列表包装为 ITool，供 Agent 调用。

**Skill 扫描外部化**：Skill 扫描和 ReadSkillTool 注册由 agent_cli 负责，agent_lib 不直接依赖 SKILL.md 文件格式。

### 11. 记忆系统

**接口**：`IMemory` 位于 `include/agent/i_memory.h`。

**方法**：`store()` / `retrieve()` / `search()` / `remove()` / `clear()` / `get_memory_name()`

Agent 内部直接持有 `MemoryPtr`（`shared_ptr<IMemory>`），通过 `agent->set_memory()` 设置。

**内置实现**：`SimpleMemory` (agent_cli) 提供基于 `std::map` 的内存存储，`search()` 使用子串匹配。

**Context 与 Memory 的关系**：

| 维度 | Context | Memory |
|------|---------|--------|
| 生命周期 | 单次 Agent 运行 | 跨次运行，持久化 |
| 数据结构 | `vector<Message>` 有序消息列表 | `map<key, value>` KV 存储 |
| LLM 可见性 | 直接可见 | 间接可见（通过 `search()` 注入为 System 消息） |
| 写入时机 | 每轮对话追加 | 交互结束时 `store("last_interaction", output)` |
| 读取时机 | 每次 LLM 调用前 `get_messages()` | 每次 `run()` 开始时 `search(user_input)` |

### 12. 人格系统

`PersonalityDocs` 结构体管理以下文档：
- `SOUL.md` (≤200 字符)
- `IDENTITY.md` (≤200 字符)
- `AGENTS.md` (≤2000 字符)
- `mcp.md` / `tools.md`
- `USER.md` + `USER-{name}.md` (≤500 字符)

Agent 内部直接持有 `PersonalityDocs` 值对象，由外部在创建 Agent 前通过 `AgentConfig.personality` 设置。

## 数据模型

### 核心类型

```
u8str = std::u8string    // UTF-8 字符串

Message { role, content, name?, tool_call_id?, tool_calls[], reasoning_content? }
ToolCall { id, name, arguments (JSON string) }
ToolResult { tool_call_id, content, is_error }
ThinkingStep { step_index, thinking_content, tool_call?, tool_result?, timestamp,
               plan_step_id?, phase?, duration_ms? }
ContextSnapshot { messages[], state, thinking_steps[], final_output? }

LlmRequest { messages[], system_prompt?, model_config, tools[] }
LlmResponse { content, tool_calls[], is_error, error_message, thinking_content? }

ConfirmRequest { action_description, details }
ConfirmResult { confirmed, feedback? }

LlmModelConfig { model_type, model_name, api_base_url, api_key, temperature, max_tokens, top_p }
AgentConfig { model_config, llm_provider?, confirm_handler?, prompt_builder?,
              context_manager?, tool_registry?, memory?, personality,
              skill_dirs, mcp_config_path }
AgentMode { ReAct, PlanAndExecute, Reflection }

PlanStep { id, description, status, result,
           depends_on[], tool_hint?, tool_args_hint?, expected_output?,
           condition?, fallback_step?, start_time, end_time, retry_count }
Plan { steps[], summary }
PlanExecutionLog { original_plan, step_logs[], replan_count, ... }
PlanStepLog { step_id, description, status, start_time, end_time, ... }
```

### Agent 状态

```cpp
enum class AgentState {
    Idle,               // 空闲
    Thinking,           // 思考中
    WaitingToolResult,  // 等待工具
    WaitingUserConfirm, // 等待用户确认/输入
    Completed,          // 完成
    Error               // 错误
};
```

### LlmModelType

```cpp
enum class LlmModelType {
    OpenAI, Claude, Kimi, DeepSeek, GLM, Custom
};
```

## 并发模型

### 工作线程 + 输入队列

```
主线程                         工作线程
  │                             │
  ├── submit_input() ──────────► input_queue_
  │                               │
  │                               ▼
  │                           IAgentLoop.run() (ReactLoop / PlanExecuteLoop / ReflectionLoop)
  │                             │
  │                             ├── call_llm()
  │                             ├── execute_tool()
  │                             ├── mcp_call()
  │                             │
  │   ◄─────────────────────────── on_thinking_update()
  │   ◄─────────────────────────── on_output_ready()
  │   ◄─────────────────────────── on_state_change()
  │                             │
  │                       process_loop()
  │                             │
  │                             ├── Auto-Continue 判断
  │                             │   ├── WaitingUserConfirm → 停下等待
  │                             │   ├── needs_user_input → 停下等待
  │                             │   └── 不需要 → 推入 "继续" ──►
  │                             │                                    │
  │                             └── queue_cv_.wait() ◄──────────────┘
  │                                   │
  ├── submit_input_interrupt()  ├── interrupt() (重置循环)
```

### 线程安全策略

| 组件 | 同步机制 |
|------|---------|
| ToolRegistry | `std::shared_mutex` |
| McpManager | `std::shared_mutex` |
| DefaultContextManager | `std::shared_mutex` |
| Agent::Impl | `std::mutex` + `std::condition_variable` |
| ReactLoop | `std::atomic` + `std::mutex` |
| PlanExecuteLoop | `std::atomic` + `std::mutex` |
| ReflectionLoop | `std::atomic` + `std::mutex` |
| McpClient | `std::mutex` |

## 模块依赖关系

```
Agent::create_react(config) / create_plan_execute(config) / create_reflection(config)
  ├── config.llm_provider → LlmProviderFactory::create()
  ├── config.confirm_handler → DefaultConfirmHandler
  ├── config.prompt_builder → PromptBuilder
  ├── config.context_manager → DefaultContextManager
  │
  └── ReactLoop / PlanExecuteLoop / ReflectionLoop (组合上述组件)
        ├── IContextManager — 读写消息
        ├── IPromptBuilder — 构建 Prompt
        ├── ILlmProvider — 调用 LLM
        ├── IToolRegistry — 查找/执行工具
        ├── IMcpManager — 查找/执行 MCP
        ├── IMemory — 检索/存储记忆
        └── PersonalityDocs const& — 读取人格文档
```

## 扩展指南

### 添加新模型
1. `types.h` 枚举 `LlmModelType` 添加类型
2. 创建 Provider 继承 `ILlmProvider`
3. `llm_provider_factory.cpp` 添加 case

### 添加新工具
1. 创建类继承 `ITool`
2. 实现所有纯虚方法
3. `agent->register_tool(ptr)` 注册

### 添加 MCP 服务
1. 在 `config/mcps/mcp.json` 添加配置
2. Stdio 方式：配置 `command` + `args` + `env`
3. HTTP 方式：配置 `url`
4. 重启 Agent 生效

### 添加浏览器 MCP 工具
1. 在 `mcp.json` 中添加配置
2. 如果需要在启动时强制 `headless=false`，在 `u8str_utils.cpp` 的 `mutable_browser_launch_tools()` 函数中添加工具名

### 添加自定义记忆
1. 创建类继承 `IMemory`
2. 实现接口方法
3. `agent->set_memory(ptr)` 注册