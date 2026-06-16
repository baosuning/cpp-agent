# Checklist

> **Status: 全部完成** (2026-06)

## 项目基础设施
- [x] CMakeLists.txt 存在且正确设置C++20标准、项目名称、编译选项
- [x] 目录结构完整（include/agent/ + include/util/、src/各子目录、third_party/、agent_cli/、tests/）
- [x] nlohmann/json.hpp 已集成到 third_party/nlohmann/
- [x] 平台条件编译宏（AGENT_WINDOWS / AGENT_LINUX）
- [x] 构建脚本（build.bat / build.ps1）

## 公共接口（include/agent/ + include/util/）
- [x] types.h — 定义 u8str、Message、ToolCall、ToolResult、ThinkingStep、ContextSnapshot、LlmRequest/LlmResponse、AgentState、LlmModelType、LlmModelConfig、PlanStep（含扩展字段）、PlanExecutionLog 等
- [x] agent.h — Agent 主类（工厂方法 create_react/create_plan_execute/create_reflection/create_custom、生命周期 start/stop、输入 submit_input/submit_input_interrupt、工具注册 register_tool、get_tool_registry/get_mcp_manager、记忆管理 set_memory、回调注册、状态获取 get_plan/get_execution_log）
- [x] agent_config.h — AgentConfig（通用基础配置）、ReactAgentConfig、PlanExecuteAgentConfig、ReflectionAgentConfig、AgentMode { ReAct, PlanAndExecute, Reflection }
- [x] personality.h — PersonalityDocs 结构体（soul/identity/agents/mcp_doc/tools_doc/user_index/user_profiles）
- [x] i_llm_provider.h — ILlmProvider 抽象接口（send_request/send_request_async）
- [x] i_tool.h — ITool 抽象接口 + IToolRegistry 抽象接口（register_tool/update_tool/remove_tool/get_tool/has_tool/list_tools/get_tools_schema）
- [x] i_mcp.h — IMcpManager 抽象接口 + IMcpClient 抽象接口
- [x] i_memory.h — IMemory 抽象接口（store/retrieve/search/remove/clear/get_memory_name）
- [x] i_prompt_builder.h — IPromptBuilder 抽象接口（build_system_prompt/build_user_prompt/build_tool_result_prompt/build_react_instruction）
- [x] i_agent_loop.h — IAgentLoop 抽象接口（run/interrupt/stop/get_thinking_steps/get_final_output/get_plan/get_execution_log/needs_user_input/should_auto_continue）
- [x] i_context_manager.h — IContextManager 抽象接口（add_*_message/get_messages/get_snapshot/clear/message_count/compress/truncate_to_messages）
- [x] i_user_confirm_handler.h — IUserConfirmHandler 抽象接口（confirm/confirm_async）
- [x] builtin_tools.h — 内置工具工厂函数（create_file_system_tools/create_web_fetch_tool/create_execute_command_tool/create_python_script_tool/create_shell_script_tool）
- [x] i_http_client.h (include/util/) — IHttpClient 抽象接口（post/get，默认超时 90000ms）

## Skill 系统（agent_lib/src/skill/）
- [x] SkillScanner 扫描 config/skills/ 目录下的 SKILL.md 文件（YAML Front Matter 格式）
- [x] ReadSkillTool 将扫描到的技能包装为 ITool 供 Agent 调用（位于 tool/ 目录）
- [x] Skill 扫描和注册由 agent_cli 负责（外部化），agent_lib 不直接依赖 SKILL.md 文件格式

## Memory 系统
- [x] IMemory 接口定义在 include/agent/i_memory.h
- [x] Agent 内部直接持有 MemoryPtr（shared_ptr<IMemory>），通过 agent->set_memory() 设置
- [x] SimpleMemory (agent_cli) 提供基于 std::map 的记忆实现

## Tool 注册表
- [x] IToolRegistry 接口定义在 include/agent/i_tool.h（register_tool/update_tool/remove_tool/get_tool/has_tool/list_tools/get_tools_schema）
- [x] ToolRegistry 实现 IToolRegistry，线程安全（std::shared_mutex）

## MCP 系统
- [x] IMcpManager 接口定义在 include/agent/i_mcp.h（register_mcp/get_mcp/list_mcps/has_mcp）
- [x] IMcpClient 接口定义在 include/agent/i_mcp.h（name/call/list_tools）
- [x] McpManager 实现 IMcpManager，线程安全（std::shared_mutex）
- [x] McpClient 实现完整的 JSON-RPC 2.0 客户端（connect/disconnect/call/list_tools/is_connected）
- [x] McpClientConfig 支持 name/description/command+args/url/timeout_ms 配置
- [x] ITransport 抽象传输层接口（send_request）
- [x] StdioTransport 实现（子进程 stdin/stdout）
- [x] HttpTransport 实现（HTTP POST）
- [x] JsonRpc 命名空间函数（Request/Response/Error/Notification）
- [x] MCP 配置解析由 agent_cli 负责（外部化），通过 agent->get_mcp_manager()->register_mcp() 注册

## 人格文档
- [x] PersonalityDocs 结构体（soul/identity/agents/mcp_doc/tools_doc/user_index/user_profiles）
- [x] Agent 内部直接持有 PersonalityDocs 值对象
- [x] 人格文档由外部通过 AgentConfig.personality 设置

## 上下文管理器
- [x] IContextManager 接口定义完整（add_*_message/get_messages/get_snapshot/clear/message_count/compress/truncate_to_messages）
- [x] DefaultContextManager 实现 IContextManager 接口（线程安全，shared_mutex 读写锁）
- [x] 支持4种消息角色（System/User/Assistant/Tool）
- [x] get_snapshot 返回消息+状态+思考过程+最终输出
- [x] truncate_to_messages(count) 用于 Reflection 模式上下文截断

## Prompt 构建器
- [x] build_system_prompt：组合 SOUL + IDENTITY + AGENTS + 工具schema + MCP 描述 + 技能列表 + OS + 日期 + 模式指令
- [x] build_user_prompt：组合用户输入 + 用户认知（User Profile）
- [x] build_tool_result_prompt：组合工具调用结果（含 is_error）
- [x] build_react_instruction：ReAct 模式指令
- [x] build_plan_instruction / build_execute_instruction：Plan-and-Execute 模式指令
- [x] reflection_instruction / critique_instruction / refine_instruction：Reflection 模式指令

## 内置工具实现
- [x] 文件系统工具集（ReadFileTool / WriteFileTool / ListDirectoryTool / CreateDirectoryTool / SearchFileTool / DeletePathTool / RenamePathTool）
- [x] Web 抓取工具（WebFetchTool：HTML→Text，超时控制）
- [x] 命令执行工具（ExecuteCommandTool：管道执行，超时控制，GBK→UTF-8，危险命令检测）
- [x] 脚本工具（PythonScriptTool / ShellScriptTool）
- [x] 危险操作需确认（DeletePathTool / RenamePathTool 的 requires_confirmation）
- [x] 所有工具支持同步+异步双接口

## LLM Provider
- [x] LlmProviderFactory 根据 LlmModelType 创建 Provider（OpenAI/Claude/Kimi/DeepSeek/GLM/Custom）
- [x] OpenAI Provider — Chat Completions API
- [x] Claude Provider — Anthropic Messages API（thinking 块）
- [x] Kimi Provider — Moonshot API（OpenAI 兼容）
- [x] DeepSeek Provider — DeepSeek API（thinking mode）
- [x] GLM Provider — OpenAI 兼容 API（reasoning_content 思考链）
- [x] WinHttpClient（Windows，TLS 1.2+1.3）/ StubHttpClient（Linux）
- [x] 超时可配置（默认 90s）

## 默认用户确认处理器
- [x] DefaultConfirmHandler 控制台 y/n 输入确认

## ReAct 循环
- [x] ReactLoop 实现完整 ReAct 推理循环（构建Prompt→调用LLM→解析响应→工具调用→反馈→继续/结束）
- [x] 支持中断机制（interrupt() + pending_inputs 队列）
- [x] 记录思考步骤（ThinkingStep）
- [x] 最大步数保护（max_steps）
- [x] 超出最大步数时使用最后一次内容或工具结果
- [x] 状态机转换（Idle→Thinking→WaitingToolResult/WaitingUserConfirm→Completed/Error）
- [x] 回调系统（on_thinking_update/on_output_ready/on_state_change）
- [x] 工具查找两级回退（IToolRegistry.get_tool → IMcpManager MCP 工具名解析）

## Plan-and-Execute 循环
- [x] PlanExecuteLoop 实现三阶段循环（Planning → Execute → Summarize）
- [x] 步骤状态跟踪（pending/in_progress/completed/failed/skipped）
- [x] 文本格式工具调用解析（兼容 GLM）
- [x] 双模型支持（Planner/Executor 独立 LLM，planner_*/executor_* 配置）
- [x] 步骤重试（max_step_retries）
- [x] Summarize 阶段低温度合成

## Reflection 循环
- [x] ReflectionLoop 实现 Generate → Critique → Refine 循环
- [x] 双模型支持（Critic 独立 LLM，critic_* 配置）
- [x] Critic 降级策略（失败后 Generator self-critique，两次失败才降级接受）
- [x] 上下文截断优化（truncate_to_messages 回退到 Phase 1 状态）
- [x] 结构化 Critic 评价（score 1-10、issues、suggestions、acceptable）
- [x] 6 项 Reflection 测试通过

## Agent 核心实现
- [x] Agent::Impl 组合所有模块（ContextManager/PromptBuilder/IToolRegistry/IMcpManager/IMemory/PersonalityDocs）
- [x] Agent 生命周期管理（create_react/create_plan_execute/create_reflection/create_custom/start/stop）
- [x] 输入提交（submit_input 队列 + submit_input_interrupt 中断）
- [x] 思考过程获取（get_thinking 返回所有 ThinkingStep）
- [x] 最终输出获取（get_output 返回 optional 结果）
- [x] 上下文快照（get_context 返回 ContextSnapshot）
- [x] 工具动态管理（register_tool/update_tool/remove_tool）
- [x] 记忆动态管理（set_memory/remove_memory）
- [x] 回调注册（set_on_thinking_update/set_on_output_ready/set_on_state_change）
- [x] 工作线程模式（worker_thread_ + 条件变量队列）
- [x] 异常捕获（try-catch 环绕 run 循环，Error 状态通知）
- [x] Pimpl 模式，对外只暴露 include/ 目录头文件
- [x] 模式特有组件（planner_llm_/executor_llm_/critic_llm_）通过 lambda 闭包捕获传递

## CLI 主程序（agent_cli/）
- [x] main.cpp 可编译运行
- [x] YAML Front Matter 解析器（agent.md 配置加载）
- [x] 自动注册所有内置工具（文件系统/Web/命令执行/脚本执行）
- [x] 环境变量驱动 API 密钥（LLM_API_KEY）和搜索引擎
- [x] config/skills/{name}/SKILL.md 自动扫描注册
- [x] config/mcps/mcp.json 通过 agent_cli 解析并注册 MCP 客户端
- [x] GBK→UTF-8 控制台编码转换
- [x] 交互式输入循环（quit 退出）
- [x] 支持 AGENT_CONFIG_DIR 环境变量指定配置路径
- [x] 支持 --mode react/plan_execute/reflection 命令行参数

## 搜索引擎实现
- [x] Bing 搜索（Bing Search API）
- [x] OpenSERP 搜索（本地代理，启动子进程 + HTTP 通信）
- [x] 博查搜索（Bocha Search API）
- [x] 火山搜索（Volcano Search API）
- [x] 百度 AI 搜索（千帆平台 AI Search API）
- [x] 统一的搜索接口封装（WebSearchToolBase 基类）

## 测试
- [x] framework_test 覆盖主要功能（配置、生命周期、工具注册、Skill/MCP/记忆操作、回调、LLM、中断、ReAct/PE/Reflection 三模式）
- [x] tool_test 覆盖内置工具（读文件、列目录、写文件、创建目录、搜索、命令执行）
- [x] 无 API Key 时自动 skip
- [x] Windows 平台编译通过

## 编译验证
- [x] Windows (LLVM-MinGW) 编译成功
- [x] 生成静态库 libagent_framework.a
- [x] 生成 CLI 主程序 agent_cli.exe
- [x] 生成测试程序 framework_test.exe / tool_test.exe
- [x] 自动拷贝 openserp.exe、obscura 相关 exe、config/ 目录到 build/agent_cli/

## 代码质量
- [x] C++20 标准（u8str、shared_mutex、atomic、optional、filesystem）
- [x] 模块间以抽象接口（纯虚类）调用
- [x] 对外接口仅在 include/ 目录，内部实现不暴露
- [x] UTF-8 统一编码（仅 IO 边界做编码转换）
- [x] 线程安全设计（shared_mutex + mutex + atomic）
- [x] UTF-8 统一编码 + sanitize_utf8 消毒（所有 LLM Provider 输出统一消毒）
- [x] 上下文滑动窗口压缩（DefaultContextManager::compress()）
- [x] 上下文截断（truncate_to_messages，Reflection 模式用）
- [x] Auto-Continue 机制（process_loop 自动检测用户输入需求，最大20次自动循环）
- [x] 主循环 300ms 延迟检测避免 Auto-Continue 期间显示 "> " 提示
- [x] on_thinking_update 增强显示（工具调用→、工具结果←、思考内容分行展示）
- [x] on_state_change 实时状态指示（[Thinking]、[Running tool...]）
- [x] Plan-and-Execute 模式完整实现（三阶段 + 步骤状态 + 文本工具调用解析 + 双模型 + Summarize）
- [x] Reflection 模式完整实现（Generate → Critique → Refine + 双模型 + 降级策略 + 上下文截断）
- [x] Obscura MCP 浏览器自动化支持（mcp.json 配置 + build 脚本拷贝）
- [x] 浏览器工具通用化（mutable_browser_launch_tools 替代硬编码）
- [x] Windows 管道错误修复（prompt_builder.cpp 使用 Windows API 替代 _popen/_pclose）
- [x] Plan-and-Execute 模式交互修复（WaitingUserConfirm 状态正确设置，auto-continue 不再误触）
- [x] 架构解耦重构（IToolRegistry/IMcpManager 接口、MemoryManager 移除、MCP/Skill 解析外部化）
- [x] 工厂方法拆分（create_react/create_plan_execute/create_reflection/create_custom）
- [x] 二维码生成工具（qr_code，含控制台打印）
- [x] 编译脚本自动拷贝 config 目录到 build/agent_cli/