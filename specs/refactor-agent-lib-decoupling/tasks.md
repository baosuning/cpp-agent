# Tasks

- [x] Task 1: 新增 IToolRegistry 接口
  - [x] SubTask 1.1: 创建 `include/agent/i_tool_registry.h`，定义 IToolRegistry 接口（register_tool/update_tool/remove_tool/get_tool/has_tool/list_tools/get_tools_schema）
  - [x] SubTask 1.2: 修改 `src/tool/tool_registry.h`，让 ToolRegistry 继承 IToolRegistry
  - [x] SubTask 1.3: Agent 类添加 `get_tool_registry() → shared_ptr<IToolRegistry>` 方法

- [x] Task 2: 新增 IMcpManager 接口
  - [x] SubTask 2.1: 创建 `include/agent/i_mcp_manager.h`，定义 IMcpManager 接口（register_mcp/get_mcp/list_mcps/has_mcp）
  - [x] SubTask 2.2: 修改 `src/mcp/mcp_manager.h`，让 McpManager 继承 IMcpManager
  - [x] SubTask 2.3: Agent 类添加 `get_mcp_manager() → shared_ptr<IMcpManager>` 方法

- [x] Task 3: 移除 MemoryManager，Agent 直接持有 MemoryPtr
  - [x] SubTask 3.1: 修改 `agent_impl.h`，将 `MemoryManager memory_` 替换为 `MemoryPtr memory_`
  - [x] SubTask 3.2: 修改 `agent_impl.cpp`，set_memory/get_memory/remove_memory 直接操作 MemoryPtr
  - [x] SubTask 3.3: 删除 `src/memory/memory_manager.h` 和 `src/memory/memory_manager.cpp`
  - [x] SubTask 3.4: 修改 CMakeLists.txt，移除 memory_manager.cpp

- [x] Task 4: 修改 Loop 构造函数参数为接口引用
  - [x] SubTask 4.1: ReactLoop 构造函数：ToolRegistry& → IToolRegistry&，McpManager& → IMcpManager&，MemoryManager& → IMemory&，PersonalityManager& → PersonalityDocs const&
  - [x] SubTask 4.2: PlanExecuteLoop 构造函数：同上
  - [x] SubTask 4.3: 修改 Loop 内部所有使用这些引用的代码，适配接口类型

- [x] Task 5: 修改 AgentLoopContext 使用接口引用
  - [x] SubTask 5.1: 修改 `agent_config.h` 中 AgentLoopContext：ToolRegistry& → IToolRegistry&，McpManager& → IMcpManager&，MemoryManager& → IMemory&，PersonalityManager& → PersonalityDocs const&
  - [x] SubTask 5.2: 修改 `agent_impl.cpp` 中构建 AgentLoopContext 的代码

- [x] Task 6: 移除 init_mcps / init_skills，简化 Agent::Impl
  - [x] SubTask 6.1: 移除 `agent_impl.h` 中的 `init_mcps()` / `init_skills()` / `mcp_json_path_` / `skills_dir_` / `skill_scanner_`
  - [x] SubTask 6.2: 移除 `agent_impl.cpp` 中的 `init_mcps()` / `init_skills()` 实现
  - [x] SubTask 6.3: 移除 `AgentConfig` 中的 `skills_dir` / `mcps_dir` 字段
  - [x] SubTask 6.4: 修改 `Agent::start()`，不再调用 init_skills/init_mcps
  - [x] SubTask 6.5: 修改 `agent_impl.h`，将 `PersonalityManager personality_` 替换为 `PersonalityDocs personality_docs_`
  - [x] SubTask 6.6: 修改所有 `personality_.get_all()` / `personality_.get_mcp_doc()` 等调用为直接访问 `personality_docs_`

- [x] Task 7: MCP/Skill 解析逻辑迁移到 agent_cli
  - [x] SubTask 7.1: 在 agent_cli 中实现 MCP 配置解析（读取 mcp.json，创建 McpClient，通过 agent->get_mcp_manager()->register_mcp() 注册）
  - [x] SubTask 7.2: 在 agent_cli 中实现 Skill 扫描（创建 SkillScanner，注册 ReadSkillTool 到 agent->get_tool_registry()）
  - [x] SubTask 7.3: 在 agent_cli 中设置 mcp_doc 和 tools_doc 到 Agent 的人格文档

- [x] Task 8: 编译验证和测试修复
  - [x] SubTask 8.1: 编译通过
  - [x] SubTask 8.2: 修复 framework_test 中的测试用例（移除 MemoryManager 依赖、适配接口变更）
  - [x] SubTask 8.3: 运行全部测试通过

# Task Dependencies
- [Task 1, Task 2] 可并行执行
- [Task 3] 独立，可并行
- [Task 4] depends on [Task 1, Task 2, Task 3]
- [Task 5] depends on [Task 1, Task 2, Task 3]
- [Task 6] depends on [Task 4, Task 5]
- [Task 7] depends on [Task 1, Task 2, Task 6]
- [Task 8] depends on [Task 6, Task 7]
