# Agent Framework

一个轻量级、C++ 原生的 AI Agent 框架库，支持 ReAct、Plan-and-Execute、Reflection三种 Agent 模式。

## 特性

- **三模式 Agent 引擎** — ReAct (Reasoning + Acting) + Plan-and-Execute (先规划再执行) + Reflection (自我反思改进)，均完整实现
- **多模型支持** — OpenAI、Claude (Anthropic)、Kimi (月之暗面)、DeepSeek (含 thinking mode)、GLM (智谱)
- **工具系统** — 文件系统、Web 抓取、命令执行、脚本执行（Python/Batch）、多种搜索引擎（Bing、Bocha、Volcano、OpenSERP、百度AI）
- **MCP 客户端** — 完整 JSON-RPC 2.0 协议实现，支持 stdio 和 HTTP 传输，内置 CloakBrowser 和 Obscura 浏览器自动化支持
- **人格系统** — 基于 Markdown 文档的 SOUL、IDENTITY、AGENTS 配置驱动
- **模块化架构** — 接口驱动设计，IAgentLoop、IPromptBuilder、IContextManager、Tool、Memory 均可动态注册/替换
- **Auto-Continue** — 中间结果自动继续执行，仅在真正需要用户输入时暂停，无需手动输入"继续"
- **实时状态输出** — 思考过程、工具调用、工具结果分阶段展示，长时间操作有明确状态指示
- **安全机制** — 危险命令检测 + 用户确认回调
- **异步执行** — 多线程支持，工作线程 + 任务队列模式
- **跨平台** — Windows (WinHTTP, LLVM-MinGW) / Linux (GCC/Clang)

## 项目结构

```
agent-framework/
├── CMakeLists.txt              # 顶层构建配置
├── build.ps1                   # PowerShell 编译脚本
├── build.bat                   # CMD 编译脚本
├── agent_lib/                  # 框架静态库
│   ├── include/
│   │   ├── agent/              # 公共接口头文件
│   │   │   ├── agent.h             # Agent 主类
│   │   │   ├── types.h             # 核心类型 (u8str, Message, ToolCall, LlmModelType, AgentMode 等)
│   │   │   ├── agent_config.h      # Agent 配置结构体
│   │   │   ├── personality.h       # 人格文档结构体
│   │   │   ├── i_tool.h            # ITool + IToolRegistry 抽象接口
│   │   │   ├── i_mcp.h             # IMcpManager + IMcpClient 抽象接口
│   │   │   ├── i_llm_provider.h    # ILlmProvider 抽象接口
│   │   │   ├── i_agent_loop.h      # IAgentLoop 抽象接口
│   │   │   ├── i_prompt_builder.h  # IPromptBuilder 抽象接口
│   │   │   ├── i_context_manager.h # IContextManager 抽象接口
│   │   │   ├── i_memory.h          # IMemory 抽象接口
│   │   │   ├── i_user_confirm_handler.h  # IUserConfirmHandler 抽象接口
│   │   │   └── builtin_tools.h     # 内置工具工厂函数声明
│   │   └── util/
│   │       └── i_http_client.h     # IHttpClient 抽象接口
│   └── src/                    # 框架核心实现
│       ├── agent/              # Agent 引擎
│       │   ├── agent_impl.h/cpp         # Agent 实现
│       │   ├── agent_loop_base.h/cpp    # AgentLoop 公共基类
│       │   ├── react_loop.h/cpp         # ReAct 循环 (IAgentLoop 实现)
│       │   ├── plan_execute_loop.h/cpp  # Plan-and-Execute 循环 (IAgentLoop 实现)
│       │   ├── plan_graph.h             # 计划图数据结构
│       │   ├── reflection_loop.h/cpp    # Reflection 循环 (IAgentLoop 实现)
│       │   ├── prompt_builder.h/cpp     # Prompt 构建器 (IPromptBuilder 实现)
│       │   └── context_manager.h/cpp    # 默认上下文管理器 (IContextManager 实现)
│       ├── llm/                # LLM Provider
│       │   ├── llm_provider_factory.h/cpp  # LlmProviderFactory
│       │   ├── openai_provider.h/cpp       # OpenAI
│       │   ├── claude_provider.h/cpp        # Claude
│       │   ├── kimi_provider.h/cpp          # Kimi
│       │   ├── deepseek_provider.h/cpp      # DeepSeek (含 thinking mode)
│       │   └── glm_provider.h/cpp           # GLM (含 reasoning_content)
│       ├── tool/               # 内置工具
│       │   ├── tool_registry.h/cpp         # 工具注册表 (线程安全)
│       │   ├── fs_tools.h/cpp              # 文件系统工具 (read/write/list/search/delete/rename/create_dir)
│       │   ├── web_fetch_tool.h/cpp        # Web 抓取 (html_to_text)
│       │   ├── execute_command_tool.h/cpp  # 命令执行 (含危险命令检测)
│       │   ├── script_tools.h/cpp          # 脚本执行 (Python/Batch)
│       │   └── read_skill_tool.h/cpp       # 读取技能的工具
│       ├── skill/              # 技能系统
│       │   └── skill_scanner.h/cpp         # 技能目录扫描器
│       ├── memory/             # 记忆系统
│       │   └── memory_manager.h/cpp        # 记忆管理器
│       ├── mcp/                # MCP 协议实现
│       │   ├── mcp_manager.h/cpp          # MCP 管理器
│       │   ├── mcp_client.h/cpp           # MCP 客户端 (JSON-RPC 2.0)
│       │   ├── mcp_transport.h/cpp        # 传输层抽象 (stdio/HTTP)
│       │   └── json_rpc.h                 # JSON-RPC 2.0 协议实体
│       ├── personality/        # 人格管理
│       │   └── personality_manager.h/cpp  # 人格文档管理器
│       ├── confirm/            # 用户确认
│       │   └── default_confirm_handler.h/cpp  # 默认确认处理器
│       └── util/               # 基础设施
│           ├── winhttp_client.cpp        # WinHTTP 客户端实现
│           ├── utf8_utils.h              # UTF-8 编码校验与清洗工具
│           ├── u8str_utils.cpp           # u8str 工具函数
│           └── log.cpp                   # 日志工具
├── agent_cli/                  # CLI 主程序
│   ├── main.cpp                # 入口：YAML 配置加载、工具注册、交互循环
│   ├── config/                 # Markdown 配置文件目录
│   │   ├── agent.md            # YAML Front Matter 配置 (模型、API 端点、Agent 模式等)
│   │   ├── SOUL.md             # 人格描述 (≤200 字符)
│   │   ├── IDENTITY.md         # 身份名片 (≤200 字符)
│   │   ├── AGENTS.md           # 行为规范 (≤2000 字符)
│   │   ├── skills/             # 自动扫描注册的技能 (16 个)
│   │   │   ├── get_cwd/SKILL.md
│   │   │   ├── anysearch/SKILL.md ...  # 搜索引擎技能
│   │   │   ├── docx/SKILL.md ...       # Word 文档操作
│   │   │   ├── pptx/SKILL.md ...       # PPT 演示文稿操作
│   │   │   ├── pdf/SKILL.md ...        # PDF 文档操作
│   │   │   ├── xlsx/SKILL.md ...       # Excel 表格操作
│   │   │   └── ...
│   │   └── mcps/               # MCP 配置文件
│   │       └── mcp.json
│   └── src/                    # CLI 本地实现
│       ├── utils/utils.h/cpp            # 编码转换、YAML Front Matter 解析
│       ├── tools/
│       │   ├── echo_tool.h/cpp           # 回声工具
│       │   └── qr_code_tool.h/cpp        # 二维码生成工具 (含控制台打印)
│       ├── memory/simple_memory.h/cpp    # 简单内存记忆 (IMemory 实现)
│       └── web_search/           # 搜索引擎适配器
│           ├── web_search_common.h/cpp    # 公共 HTTP 工具
│           ├── web_search_impl.h/cpp      # 搜索引擎注册表
│           ├── web_search_tool_base.h/cpp  # 搜索引擎公共基类
│           ├── bing_search.h/cpp          # Bing 搜索
│           ├── bocha_search.h/cpp         # 博查搜索
│           ├── volcano_search.h/cpp       # 火山引擎搜索
│           ├── openserp_search.h/cpp      # OpenSERP 本地代理搜索
│           └── baidu_ai_search.h/cpp      # 百度 AI 搜索
├── tests/                     # 测试代码
│   ├── framework_test/main.cpp   # 框架综合测试 (88 项)
│   └── tool_test/main.cpp        # 内置工具测试 (23 项)
└── third_party/               # 第三方依赖
    ├── nlohmann/json.hpp          # JSON 库
    ├── openserp/openserp.exe      # OpenSERP 本地搜索服务
    ├── obscura/                   # Obscura 无头浏览器 (Rust, MCP Server)
    │   ├── obscura.exe
    │   └── obscura-worker.exe
    └── cloakbrowser/             # CloakBrowser 浏览器 (Chromium)
```

## 架构设计

### 循环引擎架构（三模式支持）

```
Agent::create(config)
    │
    ├── AgentMode::React ──────────► ReactLoop
    │     └── Reasoning + Acting 交替循环
    │         适合简单任务，LLM 自主决定何时调用工具、何时给出最终答案
    │
    ├── AgentMode::PlanAndExecute ──► PlanExecuteLoop
    │     └── 先规划 → 再逐步执行 → 汇总
    │         适合复杂多步骤任务，支持步骤级状态跟踪和文本格式工具调用解析
    │
    ├── AgentMode::Reflection ─────► ReflectionLoop
    │     └── Generate → Critique → Refine 循环
    │         适合质量敏感型任务，通过自我批评和改进提升输出质量
    │
    └── 工厂策略: agent_impl.cpp::process_loop() 根据 config.agent_mode 创建相应 Loop
```

所有循环引擎均实现 `IAgentLoop` 接口，共享以下组件：
- `IContextManager` — 消息上下文管理
- `IPromptBuilder` — Prompt 构建（通过策略模式适配不同模式）
- `ILlmProvider` — LLM 调用
- `ToolRegistry` / `McpManager` — 工具执行
- `MemoryManager` — 记忆检索

### 核心数据流

```
User Input
    │
    ▼
Agent.submit_input()
    │
    ▼
Input Queue → Worker Thread (process_loop)
    │
    ├── 选择 Loop 类型 (ReactLoop / PlanExecuteLoop / ReflectionLoop)
    │
    ▼
IAgentLoop.run()
    │
    ├── ContextManager.add_user_message()
    ├── MemoryManager.search() 检索相关记忆
    ├── PromptBuilder.build_system_prompt() (人格+工具+MCP+技能+系统信息)
    ├── LLM Provider (ILlmProvider.send_request())
    ├── 解析 LLM 响应
    │   ├── 有 tool_calls → ToolRegistry/McpManager 查找执行
    │   ├── 无 tool_calls → 视为最终结果
    │   └── 工具结果反馈 → ContextManager.add_tool_message() → 再次调用 LLM
    ├── 循环直到 LLM 输出最终结果或达到 max_steps
    │
    ▼
ContextManager.compress()  (滑动窗口压缩)
    │
    ▼
Auto-Continue 判断 ─── 输出需要用户输入? ──► 等待用户 (显示 "> ")
    │
    └── 不需要 ──► 自动提交 "继续" → 回到 run() 下一轮
```

### 接口层

所有核心组件通过抽象接口解耦：

| 接口 | 所在目录 | 默认实现 | 可替换 |
|------|---------|---------|--------|
| ILlmProvider | include/agent/ | LlmProviderFactory 创建 | ✅ |
| IHttpClient | include/util/ | WinHttpClient | ❌ (内部创建) |
| ITool | include/agent/ | 内置工具 (fs_tools 等) | ✅ 可动态注册 |
| IToolRegistry | include/agent/ | ToolRegistry | ✅ AgentConfig 传入 |
| IMcpManager | include/agent/ | McpManager | ✅ get_mcp_manager() |
| IMemory | include/agent/ | SimpleMemory (agent_cli) | ✅ set_memory() |
| IAgentLoop | include/agent/ | ReactLoop / PlanExecuteLoop / ReflectionLoop | ✅ 通过工厂方法 (create_react/create_plan_execute/create_reflection) 选择 |
| IPromptBuilder | include/agent/ | PromptBuilder | ✅ AgentConfig 传入 |
| IContextManager | include/agent/ | DefaultContextManager | ✅ AgentConfig 传入 |
| IUserConfirmHandler | include/agent/ | DefaultConfirmHandler | ✅ AgentConfig 传入 |

### Agent 状态机

```
Idle ──► Thinking ──► WaitingToolResult ──► Thinking ──► ... ──► Completed
              │              │
              │              ▼
              │        WaitingUserConfirm ──► Thinking
              │
              └── 多轮循环，由 Auto-Continue 驱动
                  直到 LLM 输出需要用户输入的信息才停止
                                        │
                                        ▼
                                      [Error]
```

### ReAct 模式

Agent 通过 ReAct (Reasoning + Acting) 模式工作。每一轮循环：

1. 构建系统 Prompt（SOUL + IDENTITY + AGENTS + 工具schema + MCP 描述 + 技能列表 + 当前日期 + OS 信息 + ReAct 指令）
2. 调用 `ILlmProvider.send_request()` 发送请求到 LLM
3. 解析 LLM 响应：
   - **包含 tool_calls** → 查找对应工具（先 ToolRegistry → 再 McpManager）→ 执行 → 反馈结果 → 回到步骤 2
   - **不包含 tool_calls** → 视为本轮结果 → 进入 Auto-Continue 判断
4. Auto-Continue 检查 LLM 输出是否包含"请输入"、"?"等需要用户输入的模式
   - **需要用户输入** → 停下来等待用户输入
   - **不需要** → 自动提交"继续"，进入下一轮循环
5. 超出 `max_steps` → 使用最后一次 LLM 响应内容作为最终输出

### Plan-and-Execute 模式

分为两个阶段：

**Phase 1: Planning（规划阶段）**
1. 接收用户输入，搜索相关记忆
2. 调用 LLM 生成多步执行计划
3. 解析计划文本为 `Plan` 结构（支持 `Step N:` / `- xxx` / `N. xxx` 等格式）
4. 如果计划为空（简单问题），直接返回 LLM 回答

**Phase 2: Execute（执行阶段）**
1. 逐步执行每个 `PlanStep`
2. 对每个步骤：构建 step_prompt → 调用 LLM → 执行工具 → 继续循环
3. 支持**文本格式工具调用解析**（兼容 GLM 等不使用 function calling 的模型）
4. 如果 LLM 只返回文本不调用工具，最多重试 3 次，强制要求调用工具
5. 步骤 prompt 约束输出为 **一句话摘要**，禁止生成最终答案

**Phase 3: Summarize（汇总阶段）**
1. 所有步骤完成后，收集各步骤的描述和结果
2. 调用 LLM 综合所有数据生成最终答案（禁止工具调用）
3. 低温度 (0.3) 保持输出一致性
4. 追加简短执行摘要 footer（如 "Execution complete. Steps: 7/7 completed"）

**ReAct vs Plan-and-Execute vs Reflection 对比**：

| 维度 | ReAct | Plan-and-Execute | Reflection |
|------|-------|------------------|------------|
| 循环结构 | 单循环 | 三阶段（规划+执行+汇总） | Generate → Critique → Refine |
| 计划 | 无 | 有，可查询步骤状态 | 无 |
| 步骤跟踪 | 无 | 有（pending/in_progress/completed/failed） | 无 |
| LLM 调用次数 | 较少 | 较多（规划+每步执行） | 较多（生成+多轮反思） |
| 适用场景 | 简单任务 | 复杂多步骤任务 | 质量敏感型任务 |
| 文本工具调用解析 | 不支持 | 支持（兼容 GLM） | 不支持 |
| 质量自检 | 无 | 无 | 有（Critic 评分 + Refine 改进） |

切换模式仅需在 `agent.md` 配置中设置 `agent_mode: plan_execute`。

### Reflection 模式

通过 Generate → Critique → Refine 循环提升输出质量，适合代码生成、复杂推理、长文写作等质量敏感型场景。

**Phase 1: Generate（生成阶段）**
1. 构建 Reflection 专用 system prompt
2. 调用 LLM（带工具）生成初始回答
3. 保存 Phase 1 结束时的消息数量，用于后续上下文截断优化

**Phase 2: Critique + Refine（反思改进阶段）**
1. Critic LLM 评估当前回答质量，输出结构化评价（score 1-10、issues 列表、suggestions、acceptable）
2. 如果 acceptable=YES → 输出当前回答作为最终结果
3. 如果 acceptable=NO → 每轮 Refine 前截断上下文到 Phase 1 状态，避免上下文污染
4. Generator LLM 根据批评反馈改进回答，可能调用工具辅助改进
5. 重复直到通过或达到 `max_reflection_rounds`

**双模型支持**：Critic 可使用独立 LLM（`critic_*` 配置），未配置时共享主 LLM。

**Critic 降级策略**：Critic LLM 失败时，先尝试 Generator self-critique（让主 LLM 自评），两次都失败才最终降级为接受当前回答。

**ReAct vs Plan-and-Execute vs Reflection 对比**：

### Auto-Continue 机制

Agent 在每次循环结束后，自动判断是否需要继续：

```
循环完成 (LLM 返回纯文本)
    │
    ▼
检查输出内容
    │
    ├── 包含 "请输入"、"?"、"需要你" 等模式
    │   └── 停止 → 显示 "> " → 等待用户输入
    │
    └── 不包含上述模式（仅报告中间进度）
        └── 自动推入 "继续" → 立即进入下一轮循环
```

特性：
- 最大自动继续次数 20 次（防止无限循环）
- 收到真正用户输入时计数器重置
- 仅在 LLM 明确请求用户输入时停止（如"请输入验证码"）
- Plan-and-Execute 模式下，步骤状态和输出内容双重判断

### MCP 客户端架构

```
McpClient
  ├── McpClientConfig (name, description, command/args, url, timeout)
  ├── ITransport (抽象传输层)
  │   ├── StdioTransport (子进程 stdin/stdout 通信)
  │   └── HttpTransport (HTTP POST 通信)
  └── JSON-RPC 2.0 协议
      ├── Request (method, params, id)
      ├── Response (result, error)
      ├── Error (code, message, data)
      └── Notification (id 为 null 的 Request)
```

MCP 工具名解析逻辑（支持三种格式）：
1. `mcp__{server}__{tool}` — 双下划线格式
2. `{server}_{tool}` — 下划线格式（如 `obscura_browser_navigate`）
3. `{server}.{tool}` — 点号格式

浏览器启动类工具（`cloak_launch`、`browser_navigate`）自动强制设置 `headless=false`，确保浏览器窗口可见。

### 人格系统

通过 Markdown 文档配置 Agent 的行为特征：

| 文档 | 文件名 | 字符限制 | 说明 |
|------|--------|---------|------|
| SOUL | SOUL.md | ≤200 | 核心人格描述 |
| IDENTITY | IDENTITY.md | ≤200 | 身份名片 |
| AGENTS | AGENTS.md | ≤2000 | 行为规范/价值观 |

### 上下文管理

`DefaultContextManager` 维护消息历史，支持 4 种角色：
- System (系统 Prompt)
- User (用户输入)
- Assistant (LLM 回复，含 tool_calls、reasoning_content)
- Tool (工具执行结果)

提供 `get_snapshot()` 返回包含消息列表、当前状态、思考步骤、最终输出的快照。

当消息数超过 `max_context_messages` 时，自动压缩：前 1/3 消息摘要为一条 System 消息，保留后 2/3。

提供 `truncate_to_messages(count)` 方法，用于 Reflection 模式中截断上下文到指定帧，避免多轮 Refine 导致上下文污染。

## 编译

### 环境要求

- **CMake** 3.20+
- **C++ 编译器** — Clang (llvm-mingw) / GCC 11+ / MSVC 2022+
- **C++20 标准**

### 编译命令

```powershell
.\build.ps1              # 编译
.\build.ps1 rebuild      # 重新编译
.\build.ps1 clean        # 清理
.\build.ps1 build 8      # 指定并行任务数
```

> 如果 PowerShell 执行策略阻止运行脚本：`Set-ExecutionPolicy Bypass -Scope Process -Force; .\build.ps1`

### 编译产物

| 文件 | 说明 |
|------|------|
| `build/agent_lib/src/libagent_framework.a` | 静态库 |
| `build/agent_cli/agent_cli.exe` | CLI 主程序 |
| `build/tests/framework_test.exe` | 框架综合测试 (88 项) |
| `build/tests/tool_test.exe` | 工具功能测试 (23 项) |

### 运行测试

```powershell
.\build\tests\framework_test.exe
.\build\tests\tool_test.exe
```

## 快速开始

### agent.md 配置

```yaml
---
model_type: DeepSeek
model_name: deepseek-v4-flash
api_base_url: https://api.deepseek.com
temperature: 0.7
max_tokens: 8192
top_p: 0.9
max_steps: 15
enable_thinking: true
auto_confirm: true
agent_mode: react              # react, plan_execute 或 reflection
debug: false
---
```

### Reflection 模式专属配置

当 `agent_mode` 设置为 `reflection` 时，可配置以下额外参数：

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `max_reflection_rounds` | 3 | 最大 Critique → Refine 循环次数 |
| `critic_model_type` | (同主模型) | Critic 独立的 LLM 模型类型 |
| `critic_model_name` | (同主模型) | Critic 独立的模型名称 |
| `critic_api_base_url` | (同主模型) | Critic 独立的 API 端点 |
| `critic_temperature` | (同主模型) | Critic 温度参数（建议 0.3 更稳定） |
| `critic_max_tokens` | (同主模型) | Critic 最大 Token |
| `critic_top_p` | (同主模型) | Critic Top-P 采样 |

### Plan-and-Execute 模式专属配置

当 `agent_mode` 设置为 `plan_execute` 时，可配置以下额外参数：

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `max_replan_attempts` | 3 | 最大重规划次数 |
| `max_step_retries` | 2 | 单步最大重试次数 |
| `planner_model_type` | (同主模型) | Planner 独立的 LLM 模型类型 |
| `planner_model_name` | (同主模型) | Planner 独立的模型名称 |
| `planner_api_base_url` | (同主模型) | Planner 独立的 API 端点 |
| `planner_temperature` | (同主模型) | Planner 温度参数 |
| `planner_max_tokens` | (同主模型) | Planner 最大 Token |
| `planner_top_p` | (同主模型) | Planner Top-P 采样 |
| `executor_model_type` | (同主模型) | Executor 独立的 LLM 模型类型 |
| `executor_model_name` | (同主模型) | Executor 独立的模型名称 |
| `executor_api_base_url` | (同主模型) | Executor 独立的 API 端点 |
| `executor_temperature` | (同主模型) | Executor 温度参数 |
| `executor_max_tokens` | (同主模型) | Executor 最大 Token |
| `executor_top_p` | (同主模型) | Executor Top-P 采样 |

### 环境变量

| 变量 | 说明 | 必须 |
|------|------|------|
| `LLM_API_KEY` | 大模型 API 密钥 | ✅ |
| `LLM_CRITIC_API_KEY` | Critic 独立 LLM 的 API Key（回退到 `LLM_API_KEY`） | ❌ |
| `LLM_PLANNER_API_KEY` | Planner 独立 LLM 的 API Key（回退到 `LLM_API_KEY`） | ❌ |
| `LLM_EXECUTOR_API_KEY` | Executor 独立 LLM 的 API Key（回退到 `LLM_API_KEY`） | ❌ |
| `AGENT_CONFIG_DIR` | 配置文件目录路径 | ❌ (默认从 exe 所在路径推断) |
| `BING_SEARCH_KEY` | Bing 搜索 API Key | ❌ |
| `BOCHA_SEARCH_KEY` | 博查搜索 API Key | ❌ |
| `VOLCANO_SEARCH_KEY` | 火山引擎搜索 API Key | ❌ |
| `OPENSERP_SEARCH_ENGINES` | OpenSERP 搜索引擎列表 (逗号分隔，如 "bing") | ❌ |
| `BAIDU_AI_SEARCH_KEY` | 百度 AI 搜索 API Key | ❌ |

### 运行

```powershell
$env:LLM_API_KEY = "your-api-key"
.\build\agent_cli\agent_cli.exe

# 指定模式
.\build\agent_cli\agent_cli.exe --mode plan_execute
.\build\agent_cli\agent_cli.exe --mode react
.\build\agent_cli\agent_cli.exe --mode reflection

# 调试模式
.\build\agent_cli\agent_cli.exe --debug
```

### 配置文件目录

```
agent_cli/config/
├── agent.md             # 模型配置 (YAML Front Matter)
├── SOUL.md              # 人格
├── IDENTITY.md          # 身份
├── AGENTS.md            # 行为规范
├── skills/              # 自动注册的技能 (16 个)
│   ├── get_cwd/SKILL.md
│   ├── anysearch/SKILL.md ...  # 无 API Key 的搜索引擎
│   ├── docx/SKILL.md ...       # Word 文档操作
│   ├── pptx/SKILL.md ...       # PPT 操作
│   ├── pdf/SKILL.md ...        # PDF 操作
│   ├── xlsx/SKILL.md ...       # Excel 操作
│   └── ...
└── mcps/                # MCP 服务
    └── mcp.json         # MCP Server JSON 配置
```

## 核心概念

### 工具 (Tool)

实现 `ITool` 接口的可执行功能。内置工具：

| 工具名 | 说明 | 需确认 |
|--------|------|--------|
| read_file | 读取文件 | ❌ |
| write_file | 写入文件 | ❌ |
| list_directory | 列出目录 | ❌ |
| create_directory | 创建目录 | ❌ |
| search_file | 搜索文件 | ❌ |
| delete_path | 删除文件/目录 | ✅ |
| rename_path | 重命名/移动 | ✅ |
| web_fetch | 抓取网页 (HTML→Text) | ❌ |
| execute_command | 执行命令 | 危险命令时触发 |
| python_script | 执行 Python 脚本 | ❌ |
| run_script | 执行 Batch/Shell 脚本 | ❌ |
| read_skill | 读取技能文档 | ❌ |
| qr_code | 生成二维码 (含控制台打印) | ❌ |
| echo | 回声工具 (agent_cli 示例) | ❌ |

搜索工具（按环境变量条件加载）：

| 工具名 | 环境变量 |
|--------|----------|
| bing_search | `BING_SEARCH_KEY` |
| bocha_search | `BOCHA_SEARCH_KEY` |
| volcano_search | `VOLCANO_SEARCH_KEY` |
| openserp_search | `OPENSERP_SEARCH_ENGINES` |
| baidu_ai_search | `BAIDU_AI_SEARCH_KEY` |

工具通过 `agent_ptr->register_tool(tool)` 注册。

### 技能 (Skill)

Agent 可执行的复杂操作单元。通过 Markdown 文件定义：

```
config/skills/
  └── skill_name/
      └── SKILL.md    # YAML Front Matter + Markdown 格式
```

`SkillScanner` 自动扫描注册后，`ReadSkillTool` 将其包装为 `ITool` 供 Agent 调用。

### MCP (Model Context Protocol)

外部上下文服务协议，完整实现 JSON-RPC 2.0：

- **StdioTransport** — 子进程 stdin/stdout 通信
- **HttpTransport** — HTTP POST 通信
- **list_tools()** — 获取可用工具列表
- **call(method, params)** — 执行远程操作

MCP 配置通过 `config/mcps/mcp.json`：

```json
{
  "mcpServers": {
    "obscura": {
      "command": "obscura",
      "args": ["mcp"],
      "timeout": 120000
    },
    "amap-maps": {
      "url": "https://mcp.amap.com/mcp?key=YOUR_KEY"
    }
  }
}
```

当前内置的 MCP Server 配置：

| MCP Server | 传输方式 | 说明 |
|------------|----------|------|
| CloakBrowser | Stdio (cloakbrowser.exe) | 基于 Chromium 的浏览器自动化，提供导航/点击/填写/截图等工具 |
| Obscura | Stdio (本地进程) | Rust 轻量级无头浏览器，提供 navigate/snapshot/click/fill/type/evaluate 等 12 个工具 |
| 高德地图 | HTTP | 地理编码、路线规划、POI 搜索等地图工具 |

### 记忆 (Memory)

实现 `IMemory` 接口的记忆存储系统：
- `store(key, value)` — 存储
- `retrieve(key)` — 检索
- `search(query)` — 搜索
- `remove(key)` — 删除
- `clear()` — 清除

通过 `agent_ptr->set_memory(memory)` 注册。默认实现 `SimpleMemory` 基于 `std::map` 的内存存储。

### Context 与 Memory

| 维度 | Context | Memory |
|------|---------|--------|
| 生命周期 | 单次 Agent 运行 | 跨次运行，持久化 |
| 数据结构 | `vector<Message>` 有序消息列表 | `map<key, value>` KV 存储 |
| LLM 可见性 | 直接可见（`get_messages()` 就是 LLM 输入） | 间接可见（通过 `search()` 注入为 System 消息） |
| 类比 | 人的工作记忆（当前对话上下文窗口） | 人的长期记忆（经验、知识） |

## 技术栈

- **语言**: C++20 (u8str, shared_mutex, filesystem, optional, concepts)
- **构建系统**: CMake + MinGW Makefiles
- **HTTP 客户端**: WinHTTP (Windows)
- **JSON**: nlohmann/json (单头文件)
- **编译器**: Clang (llvm-mingw) / GCC 11+
- **协议**: JSON-RPC 2.0 (MCP 通信)
- **浏览器自动化**: CloakBrowser (Chromium) / Obscura (Rust)

## 开发指南

### 添加新模型

1. 在 `types.h` 的 `LlmModelType` 枚举中添加类型
2. 创建 Provider 类继承 `ILlmProvider`
3. 在 `LlmProviderFactory::create()` 中添加 case 分支

### 添加新工具

1. 创建工具类继承 `ITool`
2. 实现 `name()`, `description()`, `parameters_schema()`, `execute()`, `execute_async()`, `requires_confirmation()`
3. 使用 `agent_ptr->register_tool(tool)` 注册

### 添加 MCP 服务

1. 在 `config/mcps/mcp.json` 的 `mcpServers` 对象中添加服务器配置
2. Stdio 方式：配置 `command` + `args` + `env`
3. HTTP 方式：配置 `url`
4. 重启 Agent 生效

### 添加浏览器 MCP 工具

如果新的浏览器 MCP 工具需要在启动时强制 `headless=false`，在 `agent_lib/src/agent/u8str_utils.cpp` 的 `mutable_browser_launch_tools()` 函数中添加工具名。

### 添加自定义记忆实现

1. 创建类继承 `IMemory`
2. 实现接口中的纯虚方法
3. 调用 `agent_ptr->set_memory(custom_memory)` 注册

## 许可证

MIT License
