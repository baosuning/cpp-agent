#pragma once
#include <agent/i_tool.h>

namespace agent {

class ExecuteCommandTool : public ITool {
public:
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;
};

ToolPtr create_execute_command_tool();

} // namespace agent