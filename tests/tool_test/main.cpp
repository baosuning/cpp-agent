#include <agent/builtin_tools.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <algorithm>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string to_std(const agent::u8str& s) {
    return std::string(s.begin(), s.end());
}

agent::u8str to_u8(const std::string& s) {
    return agent::u8str(s.begin(), s.end());
}

int passed = 0;
int failed = 0;

void test_header(const std::string& name) {
    std::cout << "\n=== " << name << " ===" << std::endl;
}

void check(const std::string& name, bool ok) {
    if (ok) {
        std::cout << "  [PASS] " << name << std::endl;
        ++passed;
    } else {
        std::cout << "  [FAIL] " << name << std::endl;
        ++failed;
    }
}

void print_result(const std::string& label, const agent::u8str& result) {
    std::cout << "  " << label << ": " << to_std(result).substr(0, 500) << std::endl;
}

bool check_success(const std::string& json_str, bool expected) {
    if (expected) {
        return json_str.find("\"success\":true") != std::string::npos;
    } else {
        return json_str.find("\"success\":false") != std::string::npos;
    }
}

std::string normalize_path(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

} // anonymous namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    auto fs_tools = agent::create_file_system_tools();
    agent::ToolPtr read_tool, write_tool, list_tool, create_tool, search_tool, delete_tool, rename_tool;
    for (auto& t : fs_tools) {
        std::string n(to_std(t->name()));
        if (n == "read_file") read_tool = t;
        else if (n == "write_file") write_tool = t;
        else if (n == "list_directory") list_tool = t;
        else if (n == "create_directory") create_tool = t;
        else if (n == "search_file") search_tool = t;
        else if (n == "delete_path") delete_tool = t;
        else if (n == "rename_path") rename_tool = t;
    }
    auto exec_tool = agent::create_execute_command_tool();
    auto py_tool = agent::create_python_script_tool();
    auto sh_tool = agent::create_shell_script_tool();

    std::cout << "======================================" << std::endl;
    std::cout << "  Built-in Tools Test" << std::endl;
    std::cout << "======================================" << std::endl;

    // ==========================================
    // Test 1: ReadFileTool
    // ==========================================
    test_header("ReadFileTool");
    {
        auto self_path = normalize_path(__FILE__);
        auto result = read_tool->execute(to_u8("{\"path\": \"" + self_path + "\"}"));
        auto str = to_std(result);
        print_result("read self result", result);
        check("read itself", check_success(str, true));
        check("contains valid content", str.find("read_file") != std::string::npos);

        result = read_tool->execute(to_u8(R"({"path": "/nonexistent/file.txt"})"));
        str = to_std(result);
        print_result("read nonexistent result", result);
        check("read nonexistent file fails", check_success(str, false));
    }

    // ==========================================
    // Test 2: ListDirectoryTool
    // ==========================================
    test_header("ListDirectoryTool");
    {
        auto result = list_tool->execute(to_u8(R"({"path": "."})"));
        auto str = to_std(result);
        print_result("list current dir result", result);
        check("list current directory", check_success(str, true));
        check("contains entries", str.find("entries") != std::string::npos);

        result = list_tool->execute(to_u8(R"({"path": "/nonexistent_dir"})"));
        str = to_std(result);
        print_result("list nonexistent result", result);
        check("list nonexistent directory fails", check_success(str, false));
    }

    // ==========================================
    // Test 3: WriteFileTool + ReadFileTool
    // ==========================================
    test_header("WriteFileTool");
    {
        auto result = write_tool->execute(to_u8(R"({"path": "test_write.txt", "content": "hello from agent"})"));
        auto str = to_std(result);
        print_result("write file result", result);
        check("write file succeeds", check_success(str, true));

        auto read_result = read_tool->execute(to_u8(R"({"path": "test_write.txt"})"));
        auto read_str = to_std(read_result);
        print_result("read back result", read_result);
        check("read back correct content", read_str.find("hello from agent") != std::string::npos);

        std::remove("test_write.txt");
    }

    // ==========================================
    // Test 4: CreateDirectoryTool
    // ==========================================
    test_header("CreateDirectoryTool");
    {
        auto result = create_tool->execute(to_u8(R"({"path": "test_dir_temp"})"));
        auto str = to_std(result);
        print_result("create dir result", result);
        check("create directory succeeds", check_success(str, true));

        result = create_tool->execute(to_u8(R"({"path": "test_dir_temp"})"));
        str = to_std(result);
        print_result("create existing dir result", result);
        check("create existing directory reports already_exists", str.find("already_exists") != std::string::npos);

        std::filesystem::remove_all("test_dir_temp");
    }

    // ==========================================
    // Test 5: SearchFileTool
    // ==========================================
    test_header("SearchFileTool");
    {
        auto result = search_tool->execute(to_u8(R"({"path": ".", "pattern": ".*"})"));
        auto str = to_std(result);
        print_result("search result", result);
        check("search files succeeds", check_success(str, true));
    }

    // ==========================================
    // Test 6: ExecuteCommandTool
    // ==========================================
    test_header("ExecuteCommandTool");
    {
        auto result = exec_tool->execute(to_u8(R"({"command": "echo hello from command"})"));
        auto str = to_std(result);
        print_result("echo result", result);
        check("execute echo command", check_success(str, true));
        check("stdout contains output", str.find("hello from command") != std::string::npos);

        result = exec_tool->execute(to_u8(R"({"command": "echo test", "timeout_seconds": 5})"));
        str = to_std(result);
        print_result("timeout result", result);
        check("execute with custom timeout", check_success(str, true));

        result = exec_tool->execute(to_u8(R"({})"));
        str = to_std(result);
        print_result("missing param result", result);
        check("missing command parameter fails", check_success(str, false));
    }

    // ==========================================
    // Test 7: PythonScriptTool
    // ==========================================
    test_header("PythonScriptTool");
    {
        auto result = py_tool->execute(to_u8(R"PY({"script": "print('hello from python')"})PY"));
        auto str = to_std(result);
        print_result("python print result", result);
        check("python script succeeds", check_success(str, true));
        check("stdout contains output", str.find("hello from python") != std::string::npos);

        result = py_tool->execute(to_u8(R"PY({"script": "import json; print(json.dumps({'a': 1}))"})PY"));
        str = to_std(result);
        print_result("python json result", result);
        check("python json output", check_success(str, true));
        check("contains json result", str.find("stdout") != std::string::npos);

        result = py_tool->execute(to_u8(R"({})"));
        str = to_std(result);
        print_result("missing script param", result);
        check("missing script fails", check_success(str, false));
    }

    // ==========================================
    // Test 8: ShellScriptTool
    // ==========================================
    test_header("ShellScriptTool");
    {
#ifdef _WIN32
        auto result = sh_tool->execute(to_u8(R"({"script": "@echo hello from bat"})"));
#else
        auto result = sh_tool->execute(to_u8(R"({"script": "echo hello from shell"})"));
#endif
        auto str = to_std(result);
        print_result("script output", result);
        check("script succeeds", check_success(str, true));
        check("stdout contains output", str.find("hello from") != std::string::npos);

        result = sh_tool->execute(to_u8(R"({})"));
        str = to_std(result);
        print_result("missing script param", result);
        check("missing script fails", check_success(str, false));
    }

    // ==========================================
    // 汇总
    // ==========================================
    std::cout << "\n======================================" << std::endl;
    std::cout << "  Results: " << passed << " passed, " << failed << " failed, "
              << (passed + failed) << " total" << std::endl;
    std::cout << "======================================" << std::endl;

    return failed > 0 ? 1 : 0;
}