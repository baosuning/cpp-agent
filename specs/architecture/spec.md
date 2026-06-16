# Agent Framework 架构设计文档

## 1. 项目概述

Agent Framework 是一个轻量级、C++20 原生的 AI Agent 框架库，提供 ReAct、Plan-and-Execute 和 Reflection 三种 Agent 运行模式。框架采用接口驱动、模块化架构设计，支持多模型 LLM 接入、工具注册与执行、MCP 协议客户端、记忆管理和人格系统。

### 1.1 核心设计目标

- **可扩展性**：核心组件通过抽象接口解耦，支持动态注册和替换
- **三模式引擎**：同时支持 ReAct（推理+行动）、Plan-and-Execute（先规划再执行）和 Reflection（自我反思改进）
- **多模型支持**：统一 ILlmProvider 接口，适配多种 LLM 服务
- **协议兼容**：完整实现 MCP（Model Context Protocol）JSON-RPC 2.0 客户端

## 2. 整体架构

### 2.1 分层架构

```
┌─────────────────────────────────────────────────────┐
│                  应用层 (agent_cli)                    │
│  main.cpp / web_search / echo_tool / simple_memory   │
├─────────────────────────────────────────────────────┤
│                  框架层 (agent_lib)                    │
│  ┌───────────┬──────────┬──────────┬─────────────┐  │
│  │ Agent引擎  │ LLM      │ 工具系统  │ MCP协议     │  │
│  │ ReactLoop │ Provider │ ToolReg  │ McpClient   │  │
│  │ PlanExec  │ Factory  │ FsTools  │ McpManager  │  │
│  │ RefLoop   │ OpenAI   │ WebFetch │ Transport   │  │
│  │ AgentImpl │ Claude   │ ExecCmd  │ JsonRpc     │  │
│  │ PromptBld │ DeepSeek │ Script   │             │  │
│  │ ContextMgr│ Kimi/GLM │ SkTool   │             │  │
│  └───────────┴──────────┴──────────┴─────────────┘  │
│  ┌───────────┬──────────┬──────────┬─────────────┐  │
│  │ 人格管理   │ 技能系统  │ 记忆管理  │ 基础设施    │  │
│  │ PersonMgr │ SkScanner│ MemMgr   │ WinHttp     │  │
│  │           │          │ Default  │ u8str_utils │  │
│  │           │          │ Mem      │ ConfirmHdlr │  │
│  └───────────┴──────────┴──────────┴─────────────┘  │
├─────────────────────────────────────────────────────┤
│                  第三方依赖                            │
│  nlohmann/json · OpenSERP · CloakBrowser · Obscura  │
└─────────────────────────────────────────────────────┘
```

### 2.2 模块职责

| 模块 | 目录 | 核心职责 |
|------|------|---------|
| Agent 引擎 | `agent_lib/src/agent/` | Agent 生命周期管理、三模式循环引擎、Prompt 构建、上下文管理 |
| LLM 接入 | `agent_lib/src/llm/` | LLM Provider 工厂、各模型协议适配 |
| 工具系统 | `agent_lib/src/tool/` | 工具注册表、内置工具实现 |
| MCP 协议 | `agent_lib/src/mcp/` | MCP 客户端、传输层、JSON-RPC 2.0 |
| 人格管理 | `agent_lib/src/personality/` | 人格文档加载与缓存 |
| 技能系统 | `agent_lib/src/skill/` | 技能目录扫描与注册 |
| 记忆管理 | `agent_lib/src/memory/` | 记忆管理器 |
| CLI 应用 | `agent_cli/` | 交互式入口、搜索引擎适配器、YAML 配置加载 |
| 测试 | `tests/` | 框架集成测试、工具功能测试 |

## 3. 核心数据流

### 3.1 Agent 完整生命周期

```
┌──────────────────────────────────────────────────────┐
│                    Agent::start()                      │
│                        │                              │
│                        ▼                              │
│              创建工作线程 (Worker Thread)                │
│                        │                              │
│                        ▼                              │
│               等待 Input Queue 的输入                    │
│                        │                              │
│         ┌──────────────┴──────────────┐               │
│         ▼                             ▼                │
│  Agent::submit_input()         倾听线程接收输入          │
│         │                                              │
│         ▼                                              │
│  Input Queue 推入 → Worker Thread 唤醒                  │
│         │                                              │
│         ▼                                              │
│  process_loop() — 选择 IAgentLoop 实现                  │
│         │                                              │
│         ├── AgentMode::ReAct ──────► ReactLoop::run()  │
│         ├── AgentMode::PlanAndExec ──► PlanExecLoop::run() │
│         └── AgentMode::Reflection ───► ReflectionLoop::run() │
│         │                                              │
│         ▼                                              │
│  通知 on_output_ready_ / on_state_change_ 回调           │
│         │                                              │
│         ▼                                              │
│  等待下一次 submit_input() 或 stop()                     │
└──────────────────────────────────────────────────────┘
```

### 3.2 单次 LLM 调用数据流

```
User Input
    │
    ▼
ContextManager.add_user_message()
    │
    ▼
MemoryManager.search() → 检索相关记忆，注入 System Prompt
    │
    ▼
PromptBuilder.build_system_prompt(人格 + 工具Schema + MCP + 技能 + 系统信息)
    │
    ▼
LLM Provider.send_request()  ← 具体 Provider 实现处理协议差异
    │
    ▼
解析 LLM 响应
    │
    ├── 包含 tool_calls → ToolRegistry 或 McpManager 查找工具
    │       │                                              │
    │       ▼                                              ▼
    │  执行工具 → 结果 → ContextManager.add_tool_message() → 再次调用 LLM
    │
    └── 纯文本 → 视为本轮结果 → Auto-Continue 判断
            │
            ├── 需要用户输入 → 等待 (显示 "> ")
            └── 不需要 → 自动提交 "继续" → 下一轮
```

## 4. 三模式引擎设计

### 4.1 ReAct 模式 (ReactLoop)

```
一轮循环：
  1. 构建 System Prompt（含 ReAct 指令）
  2. 调用 LLM
  3. 解析响应：
     - tool_calls → 执行 → 反馈 → 继续
     - 纯文本 → 本轮结束
  4. Auto-Continue 判断
  5. 达到 max_steps 或 LLM 请求输入 → 停止
```

特点：
- 单循环结构，适合简单任务
- LLM 自主决定何时调用工具、何时给出最终答案
- 每轮循环包含完整的上下文（历史消息 + 最新工具结果）

### 4.2 Plan-and-Execute 模式 (PlanExecuteLoop)

```
Phase 1: 规划
  1. LLM 生成多步计划 (Plan)
  2. 解析计划文本为 PlanStep[]（支持 Step N: / - xxx / N. xxx 格式）
  3. 如计划为空 → 直接返回 LLM 回答

Phase 2: 执行
  1. 逐步骤执行
  2. 构建 step_prompt → 调用 LLM → 执行工具 → 继续
  3. 支持文本格式工具调用解析（兼容非 function calling 模型）
  4. 纯文本返回（无工具调用）最多重试 3 次

Phase 3: 汇总（新增）
  1. 所有步骤完成后调用 LLM 综合各步骤结果
  2. 传入所有步骤的描述和结果
  3. 禁止工具调用，仅做文本合成
  4. 低温度 (0.3) 保持输出一致性
```

特点：
- 两阶段结构，适合复杂多步骤任务
- 步骤状态跟踪（pending/in_progress/completed/failed）
- 步骤级依赖管理（depends_on 字段）
- 工具提示（tool_hint / tool_args_hint）辅助 LLM 选择工具

### 4.3 Reflection 模式 (ReflectionLoop)

```
Phase 1: Generate（生成阶段）
  1. 构建 Reflection 专用 system prompt
  2. 调用 LLM（带工具）生成初始回答
  3. 保存 Phase 1 结束时的消息计数，用于后续上下文截断优化

Phase 2: Critique + Refine（反思改进阶段）
  1. Critic LLM 评估当前回答质量
     → 输出结构化评价：score(1-10)、issues、suggestions、acceptable(YES/NO)
  2. 如果 acceptable=YES → 输出当前回答
  3. 如果 acceptable=NO → 截断上下文到 Phase 1 状态（避免污染）
     → Generator LLM 根据反馈改进回答
     → 可能调用工具辅助改进
  4. 重复直到通过或达到 max_reflection_rounds
```

特点：
- 适合质量敏感型任务（代码生成、复杂推理、长文写作）
- 支持双模型：Critic 可使用独立 LLM（`critic_*` 配置），未配置时共享主 LLM
- Critic 降级策略：失败后先尝试 Generator self-critique，两次都失败才最终降级
- 上下文截断优化：每轮 Refine 前回退上下文到 Phase 1 状态，避免 token 浪费

### 4.4 三模式对比

| 维度 | ReAct | Plan-and-Execute | Reflection |
|------|-------|------------------|-------------|
| 循环结构 | 单循环（推理↔行动交替） | 三阶段（规划→执行→汇总） | Generate → Critique → Refine |
| 计划生成 | 无 | LLM 生成结构化计划 | 无 |
| 步骤跟踪 | 无 | 有（状态机 + 依赖管理） | 无 |
| LLM 调用次数 | 较少 | 较多（规划 + 每步执行 + 汇总） | 较多（生成 + 多轮反思） |
| 工具调用解析 | 仅原生 function calling | 支持文本格式解析（兼容 GLM） | 仅原生 function calling |
| 适合场景 | 简单问答、单步工具 | 复杂多步骤、浏览器自动化 | 代码生成、复杂推理、质量敏感型 |
| 质量自检 | 无 | 无 | 有（Critic 评分 + Refine 改进） |

## 5. 接口层设计

### 5.1 核心接口一览

```
IAgentLoop (抽象循环引擎)
  ├── run(user_input)              # 启动循环
  ├── interrupt(reason)            # 中断
  ├── stop()                       # 停止
  ├── get_state()                  # 获取状态
  ├── get_thinking_steps()         # 获取思考步骤
  ├── get_final_output()           # 获取最终输出
  ├── needs_user_input()           # 是否需要用户输入
  ├── get_plan()                   # 获取执行计划（Plan-and-Execute 特有）
  └── 回调注册 (on_thinking_update / on_output_ready / on_state_change)

ILlmProvider (LLM 提供者)
  ├── send_request(request)        # 发送 LLM 请求
  ├── supports_streaming()         # 是否支持流式
  ├── get_provider_name()          # Provider 名称
  └── supports_thinking()          # 是否支持思考过程输出

ITool (工具)
  ├── name()                       # 工具名
  ├── description()                # 描述
  ├── parameters_schema()          # 参数 JSON Schema
  ├── execute(arguments)           # 同步执行
  ├── execute_async(args, cb)      # 异步执行（可选）
  └── requires_confirmation()      # 是否需要用户确认

IContextManager (上下文管理器)
  ├── add_system_message()         # 添加系统消息
  ├── add_user_message()           # 添加用户消息
  ├── add_assistant_message()      # 添加助手消息
  ├── add_tool_message()           # 添加工具消息
  ├── get_messages()               # 获取消息列表
  ├── compress()                   # 压缩上下文（滑动窗口）
  ├── truncate_to_messages(count)  # 截断上下文到指定帧（Reflection 模式用）
  ├── clear()                      # 清除所有消息
  ├── message_count()              # 获取消息数量
  └── get_snapshot()               # 获取上下文快照

IPromptBuilder (Prompt 构建器)
  ├── build_system_prompt(personality, instruction)  # 构建系统 Prompt
  ├── build_user_prompt(user_input, personality)     # 构建用户 Prompt
  └── build_tool_schema(tool_registry, mcp_manager)   # 构建工具 Schema

IMemory (记忆存储)
  ├── store(key, value)
  ├── retrieve(key)
  ├── search(query)
  ├── remove(key)
  └── clear()

IUserConfirmHandler (用户确认)
  └── confirm(message) → bool
```

### 5.2 接口可替换性

| 接口 | 默认实现 | 替换方式 |
|------|---------|---------|
| IAgentLoop | ReactLoop / PlanExecuteLoop / ReflectionLoop | `Agent::create_custom(factory)` |
| ILlmProvider | LlmProviderFactory 创建 | `AgentConfig::llm_provider` 直接注入 |
| ITool | 内置工具集 | `agent_ptr->register_tool()` |
| IContextManager | DefaultContextManager | `AgentConfig::context_manager` 注入 |
| IPromptBuilder | PromptBuilder | `AgentConfig::prompt_builder` 注入 |
| IMemory | SimpleMemory (agent_cli) | `agent_ptr->set_memory()` |
| IUserConfirmHandler | DefaultConfirmHandler | `AgentConfig::confirm_handler` 注入 |

## 6. 多 LLM Provider 架构

### 6.1 类继承关系

```
ILlmProvider (抽象基类)
  │
  ├── OpenAICompatibleProvider (抽象，实现 send_request 通用逻辑)
  │     ├── OpenAIProvider        # OpenAI GPT 系列
  │     ├── DeepSeekProvider      # DeepSeek（含 thinking mode）
  │     ├── KimiProvider          # 月之暗面 Kimi
  │     └── GlmProvider           # 智谱 GLM（含 reasoning_content）
  │
  └── ClaudeProvider              # Anthropic Claude（独立协议）
```

### 6.2 Provider 工厂

```cpp
// LlmProviderFactory::create() 根据 LlmModelType 创建对应 Provider
switch (config.model_type) {
    case LlmModelType::OpenAI:   return std::make_shared<OpenAIProvider>(config);
    case LlmModelType::Claude:   return std::make_shared<ClaudeProvider>(config);
    case LlmModelType::Kimi:     return std::make_shared<KimiProvider>(config);
    case LlmModelType::DeepSeek: return std::make_shared<DeepSeekProvider>(config);
    case LlmModelType::GLM:      return std::make_shared<GlmProvider>(config);
    case LlmModelType::Custom:   return std::make_shared<OpenAIProvider>(config);
}
```

### 6.3 Provider 协议差异处理

| Provider | 协议 | 思考过程 | Stream 支持 | 特殊处理 |
|----------|------|---------|-------------|---------|
| OpenAI | Chat Completions API | ❌ | ✅ | 标准 |
| Claude | Messages API | ❌ | ❌ | 独立协议，tool_use content block |
| DeepSeek | OpenAI 兼容 | ✅ (reasoning_content) | ✅ | 提取 reasoning_content 字段 |
| Kimi | OpenAI 兼容 | ❌ | ✅ | 标准 |
| GLM | OpenAI 兼容 | ✅ (reasoning_content) | ✅ | V3 模型无 tool_choice 限制 |

## 7. 工具系统设计

### 7.1 架构

```
ToolRegistry (线程安全)
  ├── register_tool(tool)       # 注册工具
  ├── remove_tool(name)         # 移除工具
  ├── get_tool(name)            # 查找工具
  ├── has_tool(name)            # 检查存在
  ├── list_tools()              # 列出所有工具
  └── update_tool(name, tool)   # 更新工具

ITool (抽象接口)
  ├── 内置工具 (agent_lib/src/tool/)
  │   ├── ReadFileTool
  │   ├── WriteFileTool
  │   ├── ListDirectoryTool
  │   ├── CreateDirectoryTool
  │   ├── SearchFileTool
  │   ├── DeletePathTool
  │   ├── RenamePathTool
  │   ├── WebFetchTool (HTML→Text)
  │   ├── ExecuteCommandTool
  │   ├── PythonScriptTool
  │   ├── ShellScriptTool
  │   └── ReadSkillTool
  │
  ├── CLI 工具 (agent_cli)
  │   ├── EchoTool
  │   └── 搜索引擎适配器 (WebSearchToolBase 子类)
  │       ├── BingSearch
  │       ├── BochaSearch
  │       ├── VolcanoSearch
  │       ├── OpenSerpSearch
  │       └── BaiduAiSearch
  │
  └── MCP 工具 (由 McpManager 动态生成)
      └── McpClient 远程工具列表
```

### 7.2 工具查找优先级

```
LLM 响应中包含 tool_calls
    │
    ▼
检查 tool_call.name 带 MCP 前缀？
    │
    ├── 是 → McpManager 解析 server/tool → 调用远程工具
    │
    └── 否 → ToolRegistry 查找本地工具
              │
              └── 找到 → 执行（检查 requires_confirmation）
                  未找到 → 返回错误信息
```

### 7.3 MCP 工具名解析

支持三种格式：
- `mcp__{server}__{tool}` — 双下划线格式（推荐）
- `{server}_{tool}` — 单下划线格式
- `{server}.{tool}` — 点号格式

浏览器启动类工具（`cloak_launch`、`browser_navigate`）自动强制设置 `headless=false`。

### 7.4 搜索引擎适配器

所有搜索引擎继承 `WebSearchToolBase`（agent_cli 层），共享 HTTP 请求和 JSON 解析逻辑，只需子类实现 `do_search(query, count)` 纯虚函数。

环境变量控制加载：
- `BING_SEARCH_KEY` → BingSearch
- `BOCHA_SEARCH_KEY` → BochaSearch
- `VOLCANO_SEARCH_KEY` → VolcanoSearch
- `OPENSERP_SEARCH_ENGINES` → OpenSerpSearch
- `BAIDU_AI_SEARCH_KEY` → BaiduAiSearch

## 8. MCP 客户端设计

### 8.1 架构

```
McpManager
  ├── 管理多个 McpClient 实例
  ├── 统一暴露远程工具接口
  └── 解析工具调用（server/tool 路由）

McpClient (JSON-RPC 2.0)
  ├── McpClientConfig
  │   ├── name / description
  │   ├── transport (stdio / http)
  │   ├── command / args / url
  │   ├── timeout
  │   └── env (环境变量替换 ${VAR})
  │
  ├── ITransport (抽象传输层)
  │   ├── StdioTransport
  │   │   ├── 创建子进程 stdin/stdout
  │   │   └── 读取 stdout 到行尾，写入 stdin
  │   │
  │   └── HttpTransport
  │       ├── HTTP POST 请求
  │       └── 超时控制
  │
  └── JSON-RPC 2.0 协议
      ├── Request → {jsonrpc, method, params, id}
      ├── Response → {jsonrpc, result/error, id}
      ├── Error → {code, message, data}
      └── Notification → {jsonrpc, method, params} (id=null)
```

### 8.2 通信流程

```
Agent (LLM)
  │
  │ tool_call: "server_toolName", args: {...}
  ▼
McpManager
  │
  │ 解析 "server_toolName" → server="server", tool="toolName"
  ▼
McpClient.call("tools/call", {name: "toolName", arguments: {...}})
  │
  │ 序列化为 JSON-RPC 2.0 Request
  ▼
Transport.send(request_json)
  │
  ├── Stdio: 写入子进程 stdin, 读取 stdout
  └── HTTP:  POST 到 url
  │
  ▼
Transport.receive() → Response JSON
  │
  ▼
McpClient 解析 → McpManager 返回 → Agent 处理
```

### 8.3 环境变量替换

MCP 配置中的 `${VAR_NAME}` 占位符在客户端初始化时自动替换为环境变量值。支持嵌套路径（如 `env.API_KEY`）。

## 9. Auto-Continue 机制

### 9.1 判断逻辑

```
LLM 返回纯文本响应
    │
    ▼
检查响应内容是否匹配需要用户输入的模式：
  ├── ├── "请输入"           ──► 需要用户输入
  ├── └── 其他模式  ──► 继续
    │
    ├── 匹配 → 停止等待用户输入，显示 "> "
    │
    └── 不匹配 → 自动提交 "继续" → 进入下一轮循环
```

### 9.2 安全机制

- 最大自动继续计数：20 次
- Plan-and-Execute 模式下，步骤状态和内容双重判断
- 用户输入重置自动继续计数器

## 10. 记忆系统设计

### 10.1 IMemory 接口

```
IMemory
  ├── store(key, value)          # 存储 KV 对
  ├── retrieve(key) → optional   # 检索
  ├── search(query) → vector     # 搜索（key/value 模糊匹配）
  ├── remove(key)                # 删除
  └── clear()                    # 清除所有
```

### 10.2 记忆与上下文的区别

| 维度 | 上下文 (Context) | 记忆 (Memory) |
|------|-----------------|---------------|
| 生命周期 | 单次 Agent 运行 | 跨运行持久化 |
| 数据结构 | `vector<Message>` 有序列表 | `map<key, value>` KV 存储 |
| LLM 可见性 | 直接消息列表输入 | 通过 `search()` 注入 System Prompt |
| 类比 | 人的工作记忆 | 人的长期记忆 |

## 11. 状态机

```
                  ┌─────────────────────────────────────┐
                  │             Agent 状态机              │
                  └─────────────────────────────────────┘

  Idle ──► Thinking ──► WaitingToolResult ──► Thinking ──► ... ──► Completed
                │              │
                │              ▼
                │        WaitingUserConfirm ──► Thinking
                │
                └── 多轮循环，由 Auto-Continue 驱动
                    直到 LLM 输出需要用户输入的信息才停止
                                          │
                                          ▼
                                        Error
```

状态转换触发条件：
- **Idle → Thinking**: `start()` 后收到 `submit_input()`
- **Thinking → WaitingToolResult**: LLM 响应包含 tool_calls
- **Thinking → Completed**: LLM 返回最终纯文本响应
- **Thinking → Error**: LLM 调用失败或工具执行异常
- **WaitingToolResult → Thinking**: 工具执行完成，结果反馈回 LLM
- **WaitingUserConfirm → Thinking**: 用户确认/拒绝

## 12. 构建系统

### 12.1 构建流程

```powershell
build.ps1
  │
  ├── 1. mkdir build (如不存在)
  ├── 2. cmake -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=clang++
  ├── 3. mingw32-make -j{N}
  │     ├── agent_lib → libagent_framework.a
  │     ├── agent_cli → agent_cli.exe
  │     └── tests → framework_test.exe, tool_test.exe
  ├── 4. 复制 third_party/openserp/openserp.exe → build/agent_cli/
  ├── 5. 复制 third_party/obscura/*.exe → build/agent_cli/
  └── 6. 复制 agent_cli/config/ → build/agent_cli/config/
```

### 12.2 构建产物

| 产物 | 路径 |
|------|------|
| 静态库 | `build/agent_lib/src/libagent_framework.a` |
| CLI 主程序 | `build/agent_cli/agent_cli.exe` |
| 框架测试 | `build/tests/framework_test.exe` |
| 工具测试 | `build/tests/tool_test.exe` |

### 12.3 编译产物目录结构

```
build/
├── agent_lib/src/libagent_framework.a
├── agent_cli/
│   ├── agent_cli.exe
│   ├── openserp.exe (自动复制)
│   ├── obscura.exe (自动复制)
│   ├── obscura-worker.exe (自动复制)
│   └── config/ (自动复制)
│       ├── agent.md
│       ├── SOUL.md / IDENTITY.md / AGENTS.md
│       ├── skills/
│       └── mcps/
├── tests/
│   ├── framework_test.exe
│   └── tool_test.exe
└── ... (CMake 中间文件)
```

## 13. 配置文件体系

### 13.1 agent.md (YAML Front Matter)

```yaml
---
model_type: DeepSeek          # LLM 模型类型
model_name: deepseek-v4-flash  # 模型名称
api_base_url: https://api.deepseek.com  # API 端点
temperature: 0.7               # 生成温度
max_tokens: 8192               # 最大 Token 数
top_p: 0.9                     # Top-P 采样
max_steps: 15                 # 最大循环步数
enable_thinking: true          # 启用思考过程输出
auto_confirm: true             # 自动确认工具执行
agent_mode: react              # Agent 模式 (react / plan_execute / reflection)
debug: false                   # 调试模式
---
```

### 13.2 环境变量配置

| 变量 | 用途 | 必须 |
|------|------|------|
| `LLM_API_KEY` | LLM API 密钥 | ✅ |
| `LLM_CRITIC_API_KEY` | Critic 独立 LLM API Key（回退到 `LLM_API_KEY`） | ❌ |
| `LLM_PLANNER_API_KEY` | Planner 独立 LLM API Key（回退到 `LLM_API_KEY`） | ❌ |
| `LLM_EXECUTOR_API_KEY` | Executor 独立 LLM API Key（回退到 `LLM_API_KEY`） | ❌ |
| `AGENT_CONFIG_DIR` | 配置文件目录 | ❌ (默认从 exe 所在路径推断) |
| `BING_SEARCH_KEY` | Bing 搜索 | ❌ |
| `BOCHA_SEARCH_KEY` | 博查搜索 | ❌ |
| `VOLCANO_SEARCH_KEY` | 火山引擎搜索 | ❌ |
| `OPENSERP_SEARCH_ENGINES` | OpenSERP 引擎 | ❌ |
| `BAIDU_AI_SEARCH_KEY` | 百度 AI 搜索 | ❌ |
| `CLOAKBROWSER_BINARY_PATH` | CloakBrowser 路径 | ❌ |

### 13.3 人格文档

| 文件 | 限制 | 说明 |
|------|------|------|
| `SOUL.md` | ≤200 字符 | 核心人格描述 |
| `IDENTITY.md` | ≤200 字符 | 身份名片 |
| `AGENTS.md` | ≤2000 字符 | 行为规范和价值观 |

## 14. 扩展点

### 14.1 添加新 LLM 模型

1. 在 `types.h` 的 `LlmModelType` 枚举中添加类型
2. 创建 Provider 类（直接继承 `ILlmProvider` 或 `OpenAICompatibleProvider`）
3. 在 `LlmProviderFactory::create()` 中添加 case 分支

### 14.2 添加新工具

1. 创建类继承 `ITool`
2. 实现纯虚方法
3. 通过 `agent_ptr->register_tool(tool)` 注册

### 14.3 添加 MCP 服务

1. 在 `config/mcps/mcp.json` 的 `mcpServers` 中添加配置
2. Stdio 方式配置 `command` + `args` + `env`
3. HTTP 方式配置 `url`
4. 环境变量占位符 `${VAR}` 自动展开

### 14.4 自定义 Agent 循环

```cpp
auto loop_factory = [](const AgentLoopContext& ctx)
    -> std::unique_ptr<IAgentLoop> {
    return std::make_unique<MyCustomLoop>(ctx);
};
auto agent = Agent::create_custom(std::move(loop_factory), config);
```

### 14.5 自定义记忆实现

1. 创建类继承 `IMemory`
2. 实现接口方法
3. 调用 `agent_ptr->set_memory(custom_memory)`

## 15. 安全机制

### 15.1 危险命令检测

`ExecuteCommandTool` 在执行前检查命令是否包含危险模式：
- `rm -rf /`、`format`、`del /f /s` 等破坏性命令
- `shutdown`、`reboot` 等系统操作

匹配到危险模式时触发 `requires_confirmation()` 返回 true。

### 15.2 用户确认回调

- 工具级：`ITool::requires_confirmation()` 返回 true
- 全局级：`IUserConfirmHandler::confirm()` 处理确认逻辑
- Auto-confirm 模式通过配置 `auto_confirm: true` 跳过确认

## 16. 设计决策记录

### 16.1 为什么用 u8str 而不是 std::string？

`u8str` 定义为 `std::basic_string<char8_t>`，用于在整个框架中统一 UTF-8 编码。避免 `std::string` (char，编码不明确) 和 `std::wstring` (UTF-16/32，跨平台问题) 的混用。

### 16.2 为什么 Agent 引擎用三模式而非单一模式？

ReAct 模式在简单任务中效率较高（LLM 调用次数少），但在复杂多步骤任务中容易丢失上下文。Plan-and-Execute 通过显式规划 - 执行 - 汇总三阶段，保证了复杂任务的结构化执行。Reflection 模式通过自我批评和改进循环，在质量敏感型任务中显著提升输出质量。三种模式互补，用户可根据任务类型选择最优模式。

### 16.3 为什么支持文本格式工具调用？

GLM 等模型的 function calling 支持不完整或格式不同，文本格式解析为这些模型提供了兼容方案。

### 16.4 为什么 MCP 工具名支持三种格式？

不同 LLM 对工具名的生成格式不一致（点号、下划线、双下划线），支持多种格式提高了兼容性。