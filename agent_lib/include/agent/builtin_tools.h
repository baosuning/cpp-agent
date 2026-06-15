#pragma once
#include "i_tool.h"
#include <string>
#include <vector>
#include <memory>

namespace agent {

std::vector<ToolPtr> create_file_system_tools();
ToolPtr create_web_fetch_tool();
ToolPtr create_execute_command_tool();
ToolPtr create_python_script_tool();
ToolPtr create_shell_script_tool();

} // namespace agent
