#include "echo_tool.h"
#include "utils/utils.h"

namespace agent_cli {

agent::u8str EchoTool::name() const {
    return strtou8("echo");
}

agent::u8str EchoTool::description() const {
    return strtou8("Echoes back the input text");
}

agent::u8str EchoTool::parameters_schema() const {
    return strtou8(R"({"type":"object","properties":{"text":{"type":"string","description":"Text to echo"}},"required":["text"]})");
}

agent::u8str EchoTool::execute(const agent::u8str& arguments) {
    return arguments;
}

void EchoTool::execute_async(const agent::u8str& arguments, std::function<void(agent::u8str)> callback) {
    callback(execute(arguments));
}

bool EchoTool::requires_confirmation() const {
    return false;
}

} // namespace agent_cli