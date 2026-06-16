# Tasks

> **Status: 全部完成** (2026-06)

- [x] Task 1: 项目基础设施搭建
  - [x] SubTask 1.1: 创建顶层CMakeLists.txt，设置C++20标准、项目名称、编译选项
  - [x] SubTask 1.2: 创建目录结构（include/agent/、include/util/、src/各子目录、third_party/、agent_cli/、tests/）
  - [x] SubTask 1.3: 下载并集成nlohmann/json.hpp到third_party/nlohmann/
  - [x] SubTask 1.4: 创建平台检测和条件编译的CMake宏定义

- [x] Task 2: 公共类型和接口定义（include/agent/ + include/util/）
  - [x] SubTask 2.1: types.h — 核心类型（Message、ToolCall、ToolResult、ThinkingStep、PlanStep、PlanExecutionLog、AgentState、LlmModelType等）
  - [x] SubTask 2.2: i_llm_provider.h — ILlmProvider抽象接口
  - [x] SubTask 2.3: i_user_confirm_handler.h — IUserConfirmHandler抽象接口
  - [x] SubTask 2.4: i_memory.h — IMemory抽象接口
  - [x] SubTask 2.5: i_tool.h — ITool + IToolRegistry抽象接口
  - [x] SubTask 2.6: i_mcp.h — IMcpManager + IMcpClient抽象接口
  - [x] SubTask 2.7: personality.h — PersonalityDocs结构体
  - [x] SubTask 2.8: agent_config.h — AgentConfig + ReactAgentConfig + PlanExecuteAgentConfig + ReflectionAgentConfig
  - [x] SubTask 2.9: agent.h — Agent主类接口（工厂方法 create_react/create_plan_execute/create_reflection/create_custom）
  - [x] SubTask 2.10: i_http_client.h — IHttpClient抽象接口
  - [x] SubTask 2.11: i_agent_loop.h — IAgentLoop抽象接口（含 get_plan/get_execution_log/needs_user_input/should_auto_continue）
  - [x] SubTask 2.12: i_prompt_builder.h — IPromptBuilder抽象接口
  - [x] SubTask 2.13: i_context_manager.h — IContextManager抽象接口（含 truncate_to_messages）
  - [x] SubTask 2.14: builtin_tools.h — 内置工具工厂函数声明

- [x] Task 3: Skill系统实现（agent_lib/src/skill/ + tool/）
  - [x] SubTask 3.1: skill_scanner.h/cpp — SkillScanner类（扫描config/skills目录，解析SKILL.md的YAML Front Matter）
  - [x] SubTask 3.2: read_skill_tool.h/cpp — ReadSkillTool类（将扫描到的技能包装为ITool供Agent调用）

- [x] Task 4: Tool注册表实现（src/tool/）
  - [x] SubTask 4.1: tool_registry.h/cpp — ToolRegistry类（实现IToolRegistry，线程安全）

- [x] Task 5: MCP系统实现（src/mcp/）
  - [x] SubTask 5.1: mcp_manager.h/cpp — McpManager类（实现IMcpManager，线程安全）
  - [x] SubTask 5.2: json_rpc.h — JSON-RPC 2.0协议
  - [x] SubTask 5.3: mcp_transport.h/cpp — ITransport传输层 + StdioTransport/HttpTransport实现
  - [x] SubTask 5.4: mcp_client.h/cpp — McpClient类（connect/disconnect/call/list_tools）

- [x] Task 6: 上下文管理器实现（src/agent/）
  - [x] SubTask 6.1: context_manager.h/cpp — DefaultContextManager实现（IContextManager，shared_mutex线程安全，含compress/truncate_to_messages）

- [x] Task 7: Prompt构建器实现（src/agent/）
  - [x] SubTask 7.1: prompt_builder.h/cpp — PromptBuilder类（build_system_prompt/build_user_prompt/build_tool_result_prompt + 三模式指令）

- [x] Task 8: LLM Provider实现（src/llm/）
  - [x] SubTask 8.1: llm_provider_factory.h/cpp — LlmProviderFactory
  - [x] SubTask 8.2: openai_provider.h/cpp — OpenAI Provider
  - [x] SubTask 8.3: claude_provider.h/cpp — Claude Provider
  - [x] SubTask 8.4: kimi_provider.h/cpp — Kimi Provider
  - [x] SubTask 8.5: deepseek_provider.h/cpp — DeepSeek Provider（thinking mode）
  - [x] SubTask 8.6: glm_provider.h/cpp — GLM Provider（reasoning_content）
  - [x] SubTask 8.7: winhttp_client.h/cpp — WinHttpClient / StubHttpClient

- [x] Task 9: 默认用户确认处理器实现（src/confirm/）
  - [x] SubTask 9.1: default_confirm_handler.h/cpp — DefaultConfirmHandler

- [x] Task 10: 内置工具实现（src/tool/）
  - [x] SubTask 10.1: fs_tools.h/cpp — 文件系统工具集（7个工具）
  - [x] SubTask 10.2: web_fetch_tool.h/cpp — WebFetchTool
  - [x] SubTask 10.3: execute_command_tool.h/cpp — ExecuteCommandTool（含危险命令检测）
  - [x] SubTask 10.4: script_tools.h/cpp — PythonScriptTool + ShellScriptTool

- [x] Task 11: ReAct循环实现（src/agent/）
  - [x] SubTask 11.1: react_loop.h/cpp — ReactLoop类（完整ReAct推理循环、中断机制、思考步骤记录、max_steps保护、状态机转换、回调系统）
  - [x] SubTask 11.2: 工具查找两级回退（IToolRegistry.get_tool → IMcpManager MCP工具名解析）

- [x] Task 12: Plan-and-Execute循环实现（src/agent/）
  - [x] SubTask 12.1: plan_execute_loop.h/cpp — PlanExecuteLoop类（Planning → Execute → Summarize 三阶段）
  - [x] SubTask 12.2: 文本格式工具调用解析（parse_text_tool_calls）
  - [x] SubTask 12.3: 双模型支持（Planner/Executor 独立 LLM）
  - [x] SubTask 12.4: 步骤状态跟踪 + 步骤重试

- [x] Task 13: Reflection循环实现（src/agent/）
  - [x] SubTask 13.1: reflection_loop.h/cpp — ReflectionLoop类（Generate → Critique → Refine）
  - [x] SubTask 13.2: 双模型支持（Critic 独立 LLM）
  - [x] SubTask 13.3: Critic 降级策略（Generator self-critique 回退）
  - [x] SubTask 13.4: 上下文截断优化（truncate_to_messages）
  - [x] SubTask 13.5: 结构化 Critic 评价解析（parse_critique_response）

- [x] Task 14: Agent核心实现（src/agent/）
  - [x] SubTask 14.1: agent_impl.h/cpp — AgentImpl类（组合所有模块，工作线程+条件变量队列，三模式工厂）
  - [x] SubTask 14.2: 模式特有组件（planner_llm_/executor_llm_/critic_llm_）通过 lambda 闭包捕获传递
  - [x] SubTask 14.3: Pimpl模式（Agent对外接口委托给Agent::Impl）

- [x] Task 15: CLI主程序开发（agent_cli/）
  - [x] SubTask 15.1: agent_cli/CMakeLists.txt
  - [x] SubTask 15.2: main.cpp — 配置加载、工具注册、MCP初始化、交互循环、--mode 参数
  - [x] SubTask 15.3: config/目录（agent.md/SOUL.md/IDENTITY.md/AGENTS.md/skills/mcps）
  - [x] SubTask 15.4: 搜索引擎封装（Bing/OpenSERP/Bocha/Volcano/百度AI搜索）
  - [x] SubTask 15.5: YAML Front Matter解析器
  - [x] SubTask 15.6: GBK→UTF-8控制台编码转换
  - [x] SubTask 15.7: MCP/Skill 解析外部化（agent_cli 负责解析并注册到 Agent）
  - [x] SubTask 15.8: 二维码生成工具（qr_code，含控制台打印）

- [x] Task 16: 测试代码开发
  - [x] SubTask 16.1: framework_test — 框架生命周期测试（配置/人格/注册/回调/中断/三模式）
  - [x] SubTask 16.2: tool_test — 内置工具功能测试

- [x] Task 17: 编译验证
  - [x] SubTask 17.1: Windows (LLVM-MinGW) 编译通过
  - [x] SubTask 17.2: 构建脚本（build.bat / build.ps1，含自动拷贝 config/ 目录）

- [x] Task 18: 文档更新
  - [x] SubTask 18.1: README.md — 架构描述、接口列表、配置说明、三模式文档
  - [x] SubTask 18.2: spec.md — 分层架构、模块说明、接口描述、三模式设计
  - [x] SubTask 18.3: checklist.md — 完整的实现清单
  - [x] SubTask 18.4: tasks.md — 任务分解和依赖关系

- [x] Task 19: Auto-Continue 与上下文管理增强
  - [x] SubTask 19.1: DefaultContextManager::compress() 滑动窗口压缩
  - [x] SubTask 19.2: process_loop 添加 check_needs_user_input() + 自动继续逻辑
  - [x] SubTask 19.3: 主循环 300ms 延迟检测
  - [x] SubTask 19.4: on_thinking_update 增强显示
  - [x] SubTask 19.5: on_state_change 实时状态指示
  - [x] SubTask 19.6: UTF-8 sanitize_utf8 消毒

- [x] Task 20: Plan-and-Execute 增强
  - [x] SubTask 20.1: PlanExecuteLoop Summarize 阶段
  - [x] SubTask 20.2: 双模型解耦（Planner/Executor 独立 LLM）
  - [x] SubTask 20.3: 步骤重试机制

- [x] Task 21: 架构解耦重构
  - [x] SubTask 21.1: IToolRegistry 接口定义和实现
  - [x] SubTask 21.2: IMcpManager 接口定义和实现
  - [x] SubTask 21.3: MemoryManager 移除，Agent 直接持有 MemoryPtr
  - [x] SubTask 21.4: PersonalityManager 简化为 PersonalityDocs 值对象
  - [x] SubTask 21.5: MCP/Skill 解析外部化到 agent_cli
  - [x] SubTask 21.6: 工厂方法拆分（create_react/create_plan_execute/create_reflection/create_custom）
  - [x] SubTask 21.7: AgentLoopContext 使用接口引用

- [x] Task 22: Obscura MCP 浏览器自动化支持
  - [x] SubTask 22.1: mcp.json 添加 obscura 配置
  - [x] SubTask 22.2: build.ps1/build.bat 添加 obscura 相关 exe 拷贝
  - [x] SubTask 22.3: 浏览器工具通用化（mutable_browser_launch_tools）

- [x] Task 23: 稳定性修复
  - [x] SubTask 23.1: prompt_builder.cpp 使用 Windows API 替代 _popen/_pclose
  - [x] SubTask 23.2: volcano_search.cpp 子进程 stderr 重定向
  - [x] SubTask 23.3: Plan-and-Execute 模式 WaitingUserConfirm 状态修复
  - [x] SubTask 23.4: 编码问题修复（GBK→UTF-8、0x0A 字节、UTF-8 消毒）
  - [x] SubTask 23.5: URL 含 # 的路由处理修复
  - [x] SubTask 23.6: Tool not found 错误修复（Skill vs Tool 区分）
  - [x] SubTask 23.7: invalid UTF-8 byte 错误修复（sanitize_utf8 + safe_dump）

# Task Dependencies
- [Task 2] depends on [Task 1]
- [Task 3, 4, 5, 6, 7, 8, 9] 可并行执行，均依赖 [Task 2]
- [Task 10] depends on [Task 4]
- [Task 11] depends on [Task 3, 4, 5, 6, 7, 8, 9, 10]
- [Task 12] depends on [Task 11, 14]（与 Task 11 共享 Agent 基础设施）
- [Task 13] depends on [Task 14]（与 Task 11 共享 Agent 基础设施）
- [Task 14] depends on [Task 11]
- [Task 15] depends on [Task 14]
- [Task 16] depends on [Task 11, 12, 13, 14]
- [Task 17] depends on [Task 15, 16]
- [Task 18] depends on [Task 17]
- [Task 19] depends on [Task 11, 14]
- [Task 20] depends on [Task 12]
- [Task 21] depends on [Task 14]
- [Task 22] depends on [Task 5]
- [Task 23] depends on [Task 11, 12, 15]