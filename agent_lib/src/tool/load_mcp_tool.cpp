#include "load_mcp_tool.h"
#include "../util/utf8_utils.h"
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace agent {

static std::string u8_to_str(const u8str& s) {
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

static u8str str_to_u8(const std::string& s) {
    return u8str(reinterpret_cast<const char8_t*>(s.data()), s.size());
}

LoadMcpToolTool::LoadMcpToolTool(IMcpManager& mcps)
    : mcps_(mcps) {}

u8str LoadMcpToolTool::name() const {
    return str_to_u8("load_mcp_tool");
}

u8str LoadMcpToolTool::description() const {
    return str_to_u8(
        "Load MCP tool definitions before using them. "
        "You MUST call this tool first to load a MCP tool's complete schema "
        "before you can call it. Supports loading multiple tools at once. "
        "Use the full tool name (e.g. 'amap-maps-streamableHTTP_maps_weather'). "
        "See the Available MCP Tools section in system prompt for the list of tool names.");
}

u8str LoadMcpToolTool::parameters_schema() const {
    nlohmann::json schema;
    schema["type"] = "object";
    schema["properties"]["tool_names"]["type"] = "array";
    schema["properties"]["tool_names"]["items"]["type"] = "string";
    schema["properties"]["tool_names"]["description"] =
        "Array of MCP tool names to load. Use full names like 'amap-maps-streamableHTTP_maps_weather'";
    schema["required"] = nlohmann::json::array({"tool_names"});
    return str_to_u8(schema.dump());
}

u8str LoadMcpToolTool::execute(const u8str& arguments) {
    std::string args_str = u8_to_str(llm::sanitize_utf8(arguments));

    try {
        auto args_json = nlohmann::json::parse(args_str);
        std::vector<std::string> tool_names;

        if (args_json.contains("tool_names") && args_json["tool_names"].is_array()) {
            for (const auto& name : args_json["tool_names"]) {
                if (name.is_string()) {
                    tool_names.push_back(name.get<std::string>());
                }
            }
        } else if (args_json.is_string()) {
            tool_names.push_back(args_json.get<std::string>());
        } else if (args_json.is_object() && args_json.contains("tool_name")) {
            tool_names.push_back(args_json["tool_name"].get<std::string>());
        } else {
            return str_to_u8("Error: Please provide 'tool_names' as an array of strings");
        }

        if (tool_names.empty()) {
            return str_to_u8("Error: No tool names specified. Available MCP tools are listed in the system prompt.");
        }

        // 收集所有可用的 MCP 工具名
        std::set<std::string> all_tools;
        auto mcps = mcps_.list_mcps();
        for (const auto& mcp : mcps) {
            auto sn = mcp->name();
            std::string server_str(sn.begin(), sn.end());
            auto tools = mcp->list_tools();
            for (const auto& t : tools) {
                if (t.contains("name")) {
                    std::string tn = t["name"].get<std::string>();
                    all_tools.insert(server_str + "_" + tn);
                }
            }
        }

        std::vector<std::string> not_found;
        for (const auto& name : tool_names) {
            if (all_tools.find(name) == all_tools.end()) {
                not_found.push_back(name);
            }
        }

        if (!not_found.empty()) {
            std::ostringstream msg;
            msg << "Not found: ";
            for (size_t i = 0; i < not_found.size(); ++i) {
                if (i > 0) msg << ", ";
                msg << not_found[i];
            }
            msg << ". Check the Available MCP Tools list for correct names.";
            return str_to_u8(msg.str());
        }

        // 返回成功消息，实际加载由 ReactLoop 处理
        std::ostringstream msg;
        msg << "Loaded ";
        for (size_t i = 0; i < tool_names.size(); ++i) {
            if (i > 0) msg << ", ";
            msg << tool_names[i];
        }
        msg << ". You can now call these tools in the next step. Continue with your task.";
        return str_to_u8(msg.str());
    } catch (const nlohmann::json::parse_error&) {
        return str_to_u8("Error: Invalid arguments. Please provide a JSON object with 'tool_names' array.");
    }
}

void LoadMcpToolTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool LoadMcpToolTool::requires_confirmation() const {
    return false;
}

} // namespace agent