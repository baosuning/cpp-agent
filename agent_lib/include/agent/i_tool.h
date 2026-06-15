#pragma once
#include "types.h"
#include <memory>
#include <functional>
#include <vector>

namespace agent {

class ITool {
public:
    virtual ~ITool() = default;

    virtual u8str name() const = 0;
    virtual u8str description() const = 0;
    virtual u8str parameters_schema() const = 0; // JSON Schema string
    virtual u8str execute(const u8str& arguments) = 0; // JSON string in, JSON string out
    virtual void execute_async(
        const u8str& arguments,
        std::function<void(u8str)> callback) = 0;
    virtual bool requires_confirmation() const = 0;
};

using ToolPtr = std::shared_ptr<ITool>;

// 工具注册表接口，提供工具的注册、查询和 Schema 生成能力
class IToolRegistry {
public:
    virtual ~IToolRegistry() = default;
    virtual void register_tool(ToolPtr tool) = 0;
    virtual void update_tool(const u8str& name, ToolPtr tool) = 0;
    virtual void remove_tool(const u8str& name) = 0;
    virtual ToolPtr get_tool(const u8str& name) const = 0;
    virtual bool has_tool(const u8str& name) const = 0;
    virtual std::vector<ToolPtr> list_tools() const = 0;
    virtual nlohmann::json get_tools_schema() const = 0;
};

using ToolRegistryPtr = std::shared_ptr<IToolRegistry>;

// 创建默认工具注册表实例
ToolRegistryPtr create_tool_registry();

} // namespace agent
