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
agent_mode: react
debug: false

# ========== Reflection 模式 ==========
max_reflection_rounds: 3

# 可选：Critic 独立 LLM（不配则共享主模型）
critic_model_type: DeepSeek
critic_model_name: deepseek-v4-flash
critic_api_base_url: https://api.deepseek.com
critic_temperature: 0.3
critic_max_tokens: 8192
critic_top_p: 1.0

# ========== Plan-and-Execute 模式 ==========
max_replan_attempts: 3
max_step_retries: 2

# 可选：Planner 独立 LLM（不配则共享主模型）
planner_model_type: DeepSeek
planner_model_name: deepseek-v4-flash
planner_api_base_url: https://api.deepseek.com
planner_temperature: 0.7
planner_max_tokens: 8192
planner_top_p: 0.9

# 可选：Executor 独立 LLM（不配则共享主模型）
executor_model_type: DeepSeek
executor_model_name: deepseek-v4-flash
executor_api_base_url: https://api.deepseek.com
executor_temperature: 0.7
executor_max_tokens: 8192
executor_top_p: 0.9
---
<!-- Agent 模式:
agent_mode: react           # ReAct 模式（默认）
agent_mode: plan_execute    # Plan-and-Execute 模式
agent_mode: reflection      # Reflection 模式（Generate → Critique → Refine 循环）
-->