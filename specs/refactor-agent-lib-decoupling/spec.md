# Agent Lib 架构解耦重构 Spec

## Why

当前 agent_lib 内部耦合过重：MCP 解析、Skill 扫描、人格配置解析等逻辑硬编码在 `Agent::Impl` 中，`ToolRegistry`、`McpManager`、`MemoryManager`、`PersonalityManager` 作为内部实现类却通过公共接口和 `AgentLoopContext` 暴露给外部。这导致：
1. agent_lib 无法独立于具体实现（如 mcp.json 格式、SKILL.md 格式）使用
2. 外部无法替换 MCP/Skill/Personality 的解析方式
3. `AgentLoopContext` 暴露内部类引用，破坏封装
4. MemoryManager 仅为 IMemory 的薄包装，增加了不必要的间接层

## What Changes

- **新增 `IToolRegistry` 接口** — 抽象工具注册/查找能力，放到公共头文件中；现有 `ToolRegistry` 改为实现类
- **新增 `IMcpManager` 接口** — 抽象 MCP 管理能力，放到公共头文件中；现有 `McpManager` 改为实现类
- **移除 `MemoryManager`** — Loop 直接使用 `IMemory&`，由外部设置 `MemoryPtr` 给 Agent
- **移除 `init_mcps()` / `init_skills()`** — MCP 和 Skill 的解析/初始化移到 agent_cli，解析后通过 Agent 接口设置
- **移除 `AgentConfig` 中的 `skills_dir` / `mcps_dir`** — 不再由 agent_lib 负责扫描
- **`AgentLoopContext` 使用接口引用** — 替换 `ToolRegistry&` → `IToolRegistry&`，`McpManager&` → `IMcpManager&`，`MemoryManager&` → `IMemory&`，`PersonalityManager&` → `PersonalityDocs const&`
- **`PersonalityManager` 简化** — 移除 `set_mcp_doc` / `set_tools_doc` 等 Agent 对外接口，mcp_doc/tools_doc 由外部直接写入 `PersonalityDocs` 后设置

## Impact

- Affected specs: 工具系统、MCP 系统、记忆系统、人格系统、AgentLoopContext
- Affected code:
  - `agent_lib/include/agent/` — 新增 `i_tool_registry.h`、`i_mcp_manager.h`，修改 `agent_config.h`、`agent.h`、`personality.h`
  - `agent_lib/src/agent/agent_impl.h/cpp` — 移除 init_mcps/init_skills，修改成员类型
  - `agent_lib/src/agent/react_loop.h/cpp` — 构造函数参数改为接口引用
  - `agent_lib/src/agent/plan_execute_loop.h/cpp` — 同上
  - `agent_lib/src/tool/tool_registry.h/cpp` — 实现 IToolRegistry
  - `agent_lib/src/mcp/mcp_manager.h/cpp` — 实现 IMcpManager
  - `agent_lib/src/memory/memory_manager.h/cpp` — 删除
  - `agent_lib/src/personality/personality_manager.h/cpp` — 简化
  - `agent_cli/main.cpp` — 承担 MCP/Skill 解析职责

## ADDED Requirements

### Requirement: IToolRegistry 接口

系统 SHALL 在公共头文件 `include/agent/i_tool_registry.h` 中提供 `IToolRegistry` 接口，包含以下方法：
- `register_tool(ToolPtr)` — 注册工具
- `update_tool(const u8str&, ToolPtr)` — 更新工具
- `remove_tool(const u8str&)` — 移除工具
- `get_tool(const u8str&) const → ToolPtr` — 查找工具
- `has_tool(const u8str&) const → bool` — 检查工具是否存在
- `list_tools() const → vector<ToolPtr>` — 列出所有工具
- `get_tools_schema() const → nlohmann::json` — 获取工具 JSON schema

现有 `ToolRegistry` 类 SHALL 实现 `IToolRegistry` 接口。

#### Scenario: 外部通过接口注册工具
- **WHEN** 外部代码持有 `IToolRegistry&` 引用
- **THEN** 可以调用 `register_tool()`、`list_tools()` 等方法，无需依赖内部 `ToolRegistry` 头文件

### Requirement: IMcpManager 接口

系统 SHALL 在公共头文件 `include/agent/i_mcp_manager.h` 中提供 `IMcpManager` 接口，包含以下方法：
- `register_mcp(McpPtr)` — 注册 MCP 客户端
- `get_mcp(const u8str&) const → McpPtr` — 获取 MCP 客户端
- `list_mcps() const → vector<McpPtr>` — 列出所有 MCP 客户端
- `has_mcp(const u8str&) const → bool` — 检查 MCP 是否存在

现有 `McpManager` 类 SHALL 实现 `IMcpManager` 接口。

#### Scenario: 外部通过接口管理 MCP
- **WHEN** 外部代码持有 `IMcpManager&` 引用
- **THEN** 可以调用 `register_mcp()`、`list_mcps()` 等方法，无需依赖内部 `McpManager` 头文件

### Requirement: Agent 提供 IToolRegistry 和 IMcpManager 访问

`Agent` 类 SHALL 提供 `get_tool_registry() → shared_ptr<IToolRegistry>` 和 `get_mcp_manager() → shared_ptr<IMcpManager>` 方法，允许外部获取接口引用进行工具和 MCP 的注册。

#### Scenario: 外部获取 ToolRegistry 接口
- **WHEN** 调用 `agent->get_tool_registry()`
- **THEN** 返回 `shared_ptr<IToolRegistry>`，可通过接口注册/查找工具

### Requirement: MCP 解析外部化

`Agent::Impl` SHALL NOT 包含 `init_mcps()` 方法和 `mcp_json_path_` 成员。MCP 的配置解析、客户端创建、注册 SHALL 由外部（agent_cli）完成，通过 `Agent::get_mcp_manager()` 获取接口后调用 `register_mcp()`。

#### Scenario: agent_cli 解析 mcp.json 并注册 MCP
- **WHEN** agent_cli 读取 mcp.json 配置
- **THEN** 创建 McpClient 实例，通过 `agent->get_mcp_manager()->register_mcp()` 注册

### Requirement: Skill 解析外部化

`Agent::Impl` SHALL NOT 包含 `init_skills()` 方法和 `skills_dir_`/`skill_scanner_` 成员。Skill 的扫描、ReadSkillTool 的创建和注册 SHALL 由外部（agent_cli）完成。

#### Scenario: agent_cli 扫描 skills 目录并注册
- **WHEN** agent_cli 扫描 skills 目录
- **THEN** 创建 SkillScanner，注册 ReadSkillTool 到 `agent->get_tool_registry()`

### Requirement: 人格配置外部化

`Agent::Impl` SHALL NOT 包含 `PersonalityManager` 成员。人格文档 SHALL 通过 `AgentConfig.personality` 或 `Agent::set_soul()` 等接口设置，Agent 内部仅持有 `PersonalityDocs` 值对象。`set_mcp_doc()` / `set_tools_doc()` SHALL 保留在 Agent 对外接口中，供外部在解析 MCP/Skill 后设置。

#### Scenario: agent_cli 解析人格文件并设置
- **WHEN** agent_cli 读取 SOUL.md / IDENTITY.md / AGENTS.md
- **THEN** 通过 `config.personality.soul` / `config.personality.identity` / `config.personality.agents` 设置

### Requirement: 移除 MemoryManager

`MemoryManager` 类 SHALL 被移除。Agent 内部直接持有 `MemoryPtr`（`shared_ptr<IMemory>`），Loop 直接使用 `IMemory&` 引用。`Agent::set_memory()` / `get_memory()` / `remove_memory()` 接口保持不变。

#### Scenario: 外部设置 Memory
- **WHEN** 调用 `agent->set_memory(make_shared<SimpleMemory>())`
- **THEN** Agent 内部直接持有 `MemoryPtr`，Loop 通过 `IMemory&` 引用访问

### Requirement: AgentLoopContext 使用接口引用

`AgentLoopContext` 中的内部类引用 SHALL 替换为接口引用：
- `ToolRegistry&` → `IToolRegistry&`
- `McpManager&` → `IMcpManager&`
- `MemoryManager&` → `IMemory&`
- `PersonalityManager&` → `PersonalityDocs const&`

#### Scenario: Custom 模式工厂函数使用接口
- **WHEN** 外部代码定义 `AgentLoopFactory`
- **THEN** lambda 参数为 `const AgentLoopContext&`，其中所有字段均为公共接口类型，无需 include 内部头文件

## MODIFIED Requirements

### Requirement: AgentConfig

`AgentConfig` SHALL 移除 `skills_dir` 和 `mcps_dir` 字段（MCP/Skill 解析已外部化）。`AgentLoopContext` 中的字段类型 SHALL 改为接口引用。

### Requirement: ReactLoop / PlanExecuteLoop 构造函数

构造函数参数 SHALL 从内部类引用改为接口引用：
- `ToolRegistry&` → `IToolRegistry&`
- `McpManager&` → `IMcpManager&`
- `MemoryManager&` → `IMemory&`
- `PersonalityManager&` → `PersonalityDocs const&`

### Requirement: Agent::start()

`Agent::start()` SHALL NOT 调用 `init_skills()` 和 `init_mcps()`。这些初始化 SHALL 由外部在 `start()` 之前完成。

## REMOVED Requirements

### Requirement: MemoryManager 类
**Reason**: 仅为 `IMemory` 的薄包装（`set_memory`/`get_memory`/代理方法），增加不必要的间接层。Agent 直接持有 `MemoryPtr` 即可。
**Migration**: 所有使用 `MemoryManager&` 的地方改为使用 `IMemory&`。

### Requirement: Agent::Impl::init_mcps()
**Reason**: MCP 解析逻辑移到 agent_cli，agent_lib 不应依赖 mcp.json 文件格式。
**Migration**: agent_cli 中实现 MCP 解析，通过 `agent->get_mcp_manager()->register_mcp()` 注册。

### Requirement: Agent::Impl::init_skills()
**Reason**: Skill 扫描逻辑移到 agent_cli，agent_lib 不应依赖 SKILL.md 文件格式。
**Migration**: agent_cli 中实现 Skill 扫描，创建 ReadSkillTool 并通过 `agent->get_tool_registry()->register_tool()` 注册。

### Requirement: AgentConfig::skills_dir / mcps_dir
**Reason**: agent_lib 不再负责扫描目录，这些路径由外部管理。
**Migration**: agent_cli 中直接使用配置目录路径。
