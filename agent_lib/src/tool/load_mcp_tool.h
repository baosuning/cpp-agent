#pragma once
#include <agent/i_tool.h>
#include <agent/i_mcp.h>
#include <string>

namespace agent {

// 按需加载 MCP 工具定义的工具
// LLM 在需要使用 MCP 工具前调用此工具加载完整 schema
// 支持批量加载：tool_names 为字符串数组
// 注意：此工具仅验证和返回结果，active_mcp_tools_ 的更新由调用方（ReactLoop）处理
class LoadMcpToolTool : public ITool {
public:
    explicit LoadMcpToolTool(IMcpManager& mcps);

    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;

private:
    IMcpManager& mcps_;
};

} // namespace agent