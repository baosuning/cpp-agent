#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <variant>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace agent {

using u8str = std::u8string;

enum class MessageRole {
    System,       // 系统消息，用于设置上下文
    User,         // 用户消息
    Assistant,    // AI助手回复
    Tool          // 工具调用结果
};

struct ToolCall {
    u8str  id;          // 工具调用唯一ID
    u8str  name;        // 工具名称
    u8str  arguments;   // 工具参数，JSON格式字符串
};

struct Message {
    MessageRole            role;               // 消息角色
    u8str                  content;            // 消息内容
    std::optional<u8str>   name;               // 可选名称，用于标识消息来源
    std::optional<u8str>   tool_call_id;       // 工具调用ID，用于关联工具结果
    std::vector<ToolCall>  tool_calls;         // 工具调用列表（仅 Assistant 消息使用）
    std::optional<u8str>   reasoning_content;  // 推理内容（DeepSeek thinking mode）
};

struct ToolResult {
    u8str    tool_call_id;      // 对应的工具调用ID
    u8str    content;           // 工具执行结果
    bool     is_error = false;  // 是否执行出错
};

struct ThinkingStep {
    int                                    step_index;        // 步骤索引，从0开始
    u8str                                  thinking_content;  // 思考内容
    std::optional<ToolCall>                tool_call;         // 本步骤的工具调用（如果有）
    std::optional<ToolResult>              tool_result;       // 工具执行结果（如果有）
    std::chrono::system_clock::time_point  timestamp;         // 时间戳

    // --- 新增字段 ---
    u8str                                  plan_step_id;       // 关联的计划步骤ID
    u8str                                  phase;              // "planning" / "execution" / "replanning" / "validation"
    std::optional<int>                     duration_ms;        // 本步耗时（毫秒）
};

enum class AgentState {
    Idle,               // 空闲，等待输入
    Thinking,           // 正在思考/推理
    WaitingToolResult,  // 等待工具执行结果
    WaitingUserConfirm, // 等待用户确认
    Completed,          // 完成，已产生最终输出
    Error               // 出错
};

struct ContextSnapshot {
    std::vector<Message>       messages;        // 对话历史
    AgentState                 state;           // 当前状态
    std::vector<ThinkingStep>  thinking_steps;  // 思考过程
    std::optional<u8str>       final_output;    // 最终输出
};

enum class LlmModelType {
    OpenAI,    // OpenAI GPT系列
    Claude,    // Anthropic Claude系列
    Kimi,      // 月之暗面 Kimi
    DeepSeek,  // DeepSeek系列
    GLM,       // 智谱 GLM系列
    Custom     // 自定义，需外部注册
};

struct PlanStep {
    u8str                              id;              // 步骤ID，如 "1", "2"
    u8str                              description;     // 步骤描述
    u8str                              status;          // pending / in_progress / completed / failed / skipped
    u8str                              result;          // 步骤执行结果

    // --- 新增字段 ---
    std::vector<u8str>                 depends_on;           // 依赖的步骤ID列表
    std::optional<u8str>               tool_hint;            // 建议使用的工具名
    std::optional<u8str>               tool_args_hint;       // 预填充的工具参数（JSON字符串）
    std::optional<u8str>               expected_output;      // 预期输出描述（用于偏差检测）
    std::optional<u8str>               condition;            // 执行条件
    std::optional<u8str>               fallback_step;        // 失败时跳转的步骤ID
    std::chrono::system_clock::time_point start_time;        // 执行开始时间
    std::chrono::system_clock::time_point end_time;          // 执行结束时间
    int                                 retry_count{0};      // 已重试次数
};

struct Plan {
    std::vector<PlanStep>  steps;           // 所有步骤
    u8str                  summary;         // 规划摘要
    int                    replan_count{0}; // 重规划次数
};

// 步骤结果校验结果
enum class StepValidation { Pass, Fail, Uncertain };

// 步骤执行日志
struct PlanStepLog {
    u8str                                  step_id;
    u8str                                  description;
    u8str                                  status;             // completed / failed / skipped / retry
    std::chrono::system_clock::time_point  start_time;
    std::chrono::system_clock::time_point  end_time;
    int                                    duration_ms{0};
    std::optional<ToolCall>                tool_call;
    std::optional<ToolResult>              tool_result;
    StepValidation                         validation_result{StepValidation::Pass};
    int                                    retry_count{0};     // 当前是第几次尝试（从0开始）
    bool                                   is_retry{false};    // 是否为重试记录
    std::optional<u8str>                   replan_reason;      // 如果触发了重规划
};

// 计划执行日志，记录完整的计划执行过程
struct PlanExecutionLog {
    u8str                                  original_plan;      // 原始计划 JSON
    std::vector<PlanStepLog>               step_logs;
    int                                    replan_count{0};
    std::chrono::system_clock::time_point  plan_start_time;
    std::chrono::system_clock::time_point  plan_end_time;
    int                                    total_duration_ms{0};
    u8str                                  final_status;       // "completed" / "partial" / "failed"
};

struct LlmModelConfig {
    LlmModelType  model_type = LlmModelType::DeepSeek;  // 模型类型
    u8str         model_name;          // 模型名称，如 "deepseek-v4-flash"
    u8str         api_base_url;        // API端点URL
    u8str         api_key;             // API密钥
    double        temperature = 0.7;   // 采样温度，控制随机性
    int           max_tokens = 8192;   // 最大生成token数
    double        top_p = 1.0;         // nucleus采样阈值
};

struct LlmRequest {
    std::vector<Message>  messages;         // 对话消息列表
    std::optional<u8str>  system_prompt;    // 可选的系统提示
    nlohmann::json        tools = nlohmann::json::array();  // 工具定义（OpenAI-compatible format）
};

// 单次 LLM 调用的 token 用量（来自 API 响应的 usage 字段）
struct TokenUsage {
    int64_t  prompt_tokens      = 0;  // 输入 token 数
    int64_t  completion_tokens  = 0;  // 输出 token 数
    int64_t  total_tokens       = 0;  // 总 token 数（部分 API 不返回，则为 0）
};

// 会话级累计 token 统计快照（可拷贝，用于返回给外部）
struct TokenUsageStats {
    int64_t  total_prompt_tokens      = 0;  // 累计输入 token
    int64_t  total_completion_tokens  = 0;  // 累计输出 token
    int64_t  total_tokens             = 0;  // 累计总 token
    int64_t  llm_call_count           = 0;  // LLM 调用次数
};

struct LlmResponse {
    u8str                        content;           // 回复内容
    std::vector<ToolCall>        tool_calls;        // 工具调用请求（如果有）
    bool                         is_error = false;  // 是否出错
    u8str                        error_message;     // 错误信息
    std::optional<u8str>         thinking_content;  // 思考内容（如Claude的thinking）
    std::optional<TokenUsage>    usage;             // 本次调用的 token 用量（若 API 返回）
};

struct ConfirmRequest {
    u8str action_description;  // 需要确认的操作描述
    u8str details;          // 详细信息
};

struct ConfirmResult {
    bool                  confirmed = false;  // 用户是否确认
    std::optional<u8str>  feedback;           // 用户的反馈信息
};

} // namespace agent
