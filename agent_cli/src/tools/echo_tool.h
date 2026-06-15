#pragma once

#include <agent/i_tool.h>

namespace agent_cli {

class EchoTool : public agent::ITool {
public:
    agent::u8str name() const override;
    agent::u8str description() const override;
    agent::u8str parameters_schema() const override;
    agent::u8str execute(const agent::u8str& arguments) override;
    void execute_async(const agent::u8str& arguments, std::function<void(agent::u8str)> callback) override;
    bool requires_confirmation() const override;
};

} // namespace agent_cli