#include "fs_tools.h"
#include "../util/utf8_utils.h"
#include <util/u8str_utils.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>

namespace fs = std::filesystem;
namespace agent {

namespace {

using json = nlohmann::json;

// 安全的 JSON 序列化，遇到无效 UTF-8 字节时用 U+FFFD 替换而非抛出异常
static std::string safe_dump(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string safe_resolve(const std::string& path_str) {
    auto p = fs::weakly_canonical(fs::absolute(path_str));
    return p.string();
}

// 将 glob 模式转换为正则表达式（支持 * 和 ? 通配符）
std::string glob_to_regex(const std::string& glob) {
    std::string regex;
    regex.reserve(glob.size() * 2 + 2);
    regex += '^';
    for (char c : glob) {
        switch (c) {
            case '*': regex += ".*"; break;
            case '?': regex += '.';  break;
            case '.': regex += "\\."; break;
            case '^': case '$': case '\\': case '(': case ')':
            case '[': case ']': case '{': case '}': case '+': case '|':
                regex += '\\';
                regex += c;
                break;
            default:
                regex += c;
        }
    }
    regex += '$';
    return regex;
}

json parse_args(const u8str& arguments) {
    try {
        return json::parse(u8str_util::to_string(llm::sanitize_utf8(arguments)));
    } catch (...) {
        return json::object();
    }
}

u8str result_json(bool success, const std::string& data, const std::string& error = "") {
    json j;
    j["success"] = success;
    if (success) {
        j["result"] = data;
    } else {
        j["error"] = error;
    }
    return u8str_util::to_u8str(safe_dump(j));
}

} // anonymous namespace

// ============================================================
// ReadFileTool
// ============================================================

u8str ReadFileTool::name() const { return u8str_util::to_u8str("read_file"); }

u8str ReadFileTool::description() const {
    return u8str_util::to_u8str("Read the contents of a file at the specified path");
}

u8str ReadFileTool::parameters_schema() const {
    return u8str_util::to_u8str(R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the file"}
        },
        "required": ["path"]
    })");
}

u8str ReadFileTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto path_it = args.find("path");
    if (path_it == args.end() || !path_it->is_string()) {
        return result_json(false, "", "Missing required parameter: path");
    }

    try {
        auto resolved = safe_resolve(path_it->get<std::string>());
        if (!fs::exists(resolved)) {
            return result_json(false, "", "File not found: " + resolved);
        }
        if (!fs::is_regular_file(resolved)) {
            return result_json(false, "", "Not a regular file: " + resolved);
        }

        std::ifstream file(resolved, std::ios::binary);
        if (!file.is_open()) {
            return result_json(false, "", "Cannot open file: " + resolved);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return result_json(true, buffer.str());
    } catch (const fs::filesystem_error& e) {
        return result_json(false, "", e.what());
    }
}

void ReadFileTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool ReadFileTool::requires_confirmation() const { return false; }

// ============================================================
// WriteFileTool
// ============================================================

u8str WriteFileTool::name() const { return u8str_util::to_u8str("write_file"); }

u8str WriteFileTool::description() const {
    return u8str_util::to_u8str("Write content to a file. Creates the file if it does not exist, overwrites if it does");
}

u8str WriteFileTool::parameters_schema() const {
    return u8str_util::to_u8str(R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the file"},
            "content": {"type": "string", "description": "Content to write"}
        },
        "required": ["path", "content"]
    })");
}

u8str WriteFileTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto path_it = args.find("path");
    auto content_it = args.find("content");
    if (path_it == args.end() || !path_it->is_string()) {
        return result_json(false, "", "Missing required parameter: path");
    }
    if (content_it == args.end() || !content_it->is_string()) {
        return result_json(false, "", "Missing required parameter: content");
    }

    try {
        auto resolved = safe_resolve(path_it->get<std::string>());
        auto parent = fs::path(resolved).parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            fs::create_directories(parent);
        }

        std::ofstream file(resolved, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return result_json(false, "", "Cannot open file for writing: " + resolved);
        }
        auto content = content_it->get<std::string>();
        file.write(content.data(), content.size());
        file.close();

        json result;
        result["path"] = resolved;
        result["size"] = content.size();
        return result_json(true, result.dump());
    } catch (const fs::filesystem_error& e) {
        return result_json(false, "", e.what());
    }
}

void WriteFileTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool WriteFileTool::requires_confirmation() const { return false; }

// ============================================================
// ListDirectoryTool
// ============================================================

u8str ListDirectoryTool::name() const { return u8str_util::to_u8str("list_directory"); }

u8str ListDirectoryTool::description() const {
    return u8str_util::to_u8str("List all files and directories at the specified path");
}

u8str ListDirectoryTool::parameters_schema() const {
    return u8str_util::to_u8str(R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the directory"}
        },
        "required": ["path"]
    })");
}

u8str ListDirectoryTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto path_it = args.find("path");
    if (path_it == args.end() || !path_it->is_string()) {
        return result_json(false, "", "Missing required parameter: path");
    }

    try {
        auto resolved = safe_resolve(path_it->get<std::string>());
        if (!fs::exists(resolved)) {
            return result_json(false, "", "Directory not found: " + resolved);
        }
        if (!fs::is_directory(resolved)) {
            return result_json(false, "", "Not a directory: " + resolved);
        }

        json entries = json::array();
        for (const auto& entry : fs::directory_iterator(resolved)) {
            json item;
            item["name"] = entry.path().filename().string();
            item["type"] = entry.is_directory() ? "directory" : "file";
            item["size"] = entry.is_regular_file() ? fs::file_size(entry.path()) : 0;
            entries.push_back(std::move(item));
        }

        json result;
        result["path"] = resolved;
        result["entries"] = std::move(entries);
        return result_json(true, result.dump());
    } catch (const fs::filesystem_error& e) {
        return result_json(false, "", e.what());
    }
}

void ListDirectoryTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool ListDirectoryTool::requires_confirmation() const { return false; }

// ============================================================
// CreateDirectoryTool
// ============================================================

u8str CreateDirectoryTool::name() const { return u8str_util::to_u8str("create_directory"); }

u8str CreateDirectoryTool::description() const {
    return u8str_util::to_u8str("Create a new directory. Creates parent directories if needed");
}

u8str CreateDirectoryTool::parameters_schema() const {
    return u8str_util::to_u8str(R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the directory to create"}
        },
        "required": ["path"]
    })");
}

u8str CreateDirectoryTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto path_it = args.find("path");
    if (path_it == args.end() || !path_it->is_string()) {
        return result_json(false, "", "Missing required parameter: path");
    }

    try {
        auto resolved = safe_resolve(path_it->get<std::string>());
        if (fs::exists(resolved)) {
            if (fs::is_directory(resolved)) {
                json result;
                result["path"] = resolved;
                result["already_exists"] = true;
                return result_json(true, result.dump());
            }
            return result_json(false, "", "Path exists but is not a directory: " + resolved);
        }

        bool created = fs::create_directories(resolved);
        json result;
        result["path"] = resolved;
        result["created"] = created;
        return result_json(true, result.dump());
    } catch (const fs::filesystem_error& e) {
        return result_json(false, "", e.what());
    }
}

void CreateDirectoryTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool CreateDirectoryTool::requires_confirmation() const { return false; }

// ============================================================
// SearchFileTool
// ============================================================

u8str SearchFileTool::name() const { return u8str_util::to_u8str("search_file"); }

u8str SearchFileTool::description() const {
    return u8str_util::to_u8str("Search for files matching a glob pattern in a directory (recursively)");
}

u8str SearchFileTool::parameters_schema() const {
    return u8str_util::to_u8str(R"schema({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Directory to search in"},
            "pattern": {"type": "string", "description": "Glob pattern to match (e.g. *.cpp, test*, *.md)"}
        },
        "required": ["path", "pattern"]
    })schema");
}

u8str SearchFileTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto path_it = args.find("path");
    auto pattern_it = args.find("pattern");
    if (path_it == args.end() || !path_it->is_string()) {
        return result_json(false, "", "Missing required parameter: path");
    }
    if (pattern_it == args.end() || !pattern_it->is_string()) {
        return result_json(false, "", "Missing required parameter: pattern");
    }

    try {
        auto resolved = safe_resolve(path_it->get<std::string>());
        if (!fs::exists(resolved)) {
            return result_json(false, "", "Directory not found: " + resolved);
        }
        if (!fs::is_directory(resolved)) {
            return result_json(false, "", "Not a directory: " + resolved);
        }

        auto pattern = pattern_it->get<std::string>();
        json matches = json::array();

        for (const auto& entry : fs::recursive_directory_iterator(resolved)) {
            if (entry.is_regular_file()) {
                auto filename = entry.path().filename().string();
                if (std::regex_match(filename, std::regex(glob_to_regex(pattern)))) {
                    matches.push_back(entry.path().string());
                }
            }
        }

        json result;
        result["path"] = resolved;
        result["pattern"] = pattern;
        result["matches"] = std::move(matches);
        return result_json(true, result.dump());
    } catch (const fs::filesystem_error& e) {
        return result_json(false, "", e.what());
    }
}

void SearchFileTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool SearchFileTool::requires_confirmation() const { return false; }

// ============================================================
// DeletePathTool
// ============================================================

u8str DeletePathTool::name() const { return u8str_util::to_u8str("delete_path"); }

u8str DeletePathTool::description() const {
    return u8str_util::to_u8str("Delete a file or directory at the specified path. Use with caution!");
}

u8str DeletePathTool::parameters_schema() const {
    return u8str_util::to_u8str(R"({
        "type": "object",
        "properties": {
            "path": {"type": "string", "description": "Path to the file or directory to delete"},
            "recursive": {"type": "boolean", "description": "If true and path is a directory, delete recursively", "default": false}
        },
        "required": ["path"]
    })");
}

u8str DeletePathTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto path_it = args.find("path");
    if (path_it == args.end() || !path_it->is_string()) {
        return result_json(false, "", "Missing required parameter: path");
    }

    try {
        auto resolved = safe_resolve(path_it->get<std::string>());
        if (!fs::exists(resolved)) {
            return result_json(false, "", "Path not found: " + resolved);
        }

        bool recursive = false;
        auto rec_it = args.find("recursive");
        if (rec_it != args.end() && rec_it->is_boolean()) {
            recursive = rec_it->get<bool>();
        }

        if (fs::is_directory(resolved)) {
            if (!recursive) {
                return result_json(false, "", "Cannot delete directory without recursive flag: " + resolved);
            }
            fs::remove_all(resolved);
        } else {
            fs::remove(resolved);
        }

        json result;
        result["path"] = resolved;
        result["deleted"] = true;
        return result_json(true, result.dump());
    } catch (const fs::filesystem_error& e) {
        return result_json(false, "", e.what());
    }
}

void DeletePathTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool DeletePathTool::requires_confirmation() const { return true; }

// ============================================================
// RenamePathTool
// ============================================================

u8str RenamePathTool::name() const { return u8str_util::to_u8str("rename_path"); }

u8str RenamePathTool::description() const {
    return u8str_util::to_u8str("Rename or move a file or directory from source path to destination path");
}

u8str RenamePathTool::parameters_schema() const {
    return u8str_util::to_u8str(R"({
        "type": "object",
        "properties": {
            "source": {"type": "string", "description": "Current path of the file or directory"},
            "destination": {"type": "string", "description": "New path for the file or directory"}
        },
        "required": ["source", "destination"]
    })");
}

u8str RenamePathTool::execute(const u8str& arguments) {
    auto args = parse_args(arguments);
    auto src_it = args.find("source");
    auto dst_it = args.find("destination");
    if (src_it == args.end() || !src_it->is_string()) {
        return result_json(false, "", "Missing required parameter: source");
    }
    if (dst_it == args.end() || !dst_it->is_string()) {
        return result_json(false, "", "Missing required parameter: destination");
    }

    try {
        auto resolved_src = safe_resolve(src_it->get<std::string>());
        if (!fs::exists(resolved_src)) {
            return result_json(false, "", "Source path not found: " + resolved_src);
        }

        auto dst_path = fs::path(dst_it->get<std::string>());
        auto resolved_dst = fs::weakly_canonical(fs::absolute(dst_path));

        fs::rename(resolved_src, resolved_dst);

        json result;
        result["source"] = resolved_src;
        result["destination"] = resolved_dst.string();
        return result_json(true, result.dump());
    } catch (const fs::filesystem_error& e) {
        return result_json(false, "", e.what());
    }
}

void RenamePathTool::execute_async(const u8str& arguments, std::function<void(u8str)> callback) {
    callback(execute(arguments));
}

bool RenamePathTool::requires_confirmation() const { return true; }

// ============================================================
// Factory
// ============================================================

std::vector<ToolPtr> create_file_system_tools() {
    std::vector<ToolPtr> tools;
    tools.push_back(std::make_shared<ReadFileTool>());
    tools.push_back(std::make_shared<WriteFileTool>());
    tools.push_back(std::make_shared<ListDirectoryTool>());
    tools.push_back(std::make_shared<CreateDirectoryTool>());
    tools.push_back(std::make_shared<SearchFileTool>());
    tools.push_back(std::make_shared<DeletePathTool>());
    tools.push_back(std::make_shared<RenamePathTool>());
    return tools;
}

} // namespace agent