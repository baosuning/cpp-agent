#pragma once
#include <agent/i_tool.h>
#include <vector>

namespace agent {

class ReadFileTool : public ITool {
public:
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;
};

class WriteFileTool : public ITool {
public:
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;
};

class ListDirectoryTool : public ITool {
public:
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;
};

class CreateDirectoryTool : public ITool {
public:
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;
};

class SearchFileTool : public ITool {
public:
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;
};

class DeletePathTool : public ITool {
public:
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;
};

class RenamePathTool : public ITool {
public:
    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;
};

std::vector<ToolPtr> create_file_system_tools();

} // namespace agent