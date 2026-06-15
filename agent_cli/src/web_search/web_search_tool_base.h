#pragma once
// agent_cli/src/web_search/web_search_tool_base.h
// WebSearchToolBase：为 Bing/Bocha/BaiduAI/OpenSerp/Volcano 搜索工具提供公共基类
//
// 设计目标：
//   1. 消除 5 个 search tool 的 execute()/execute_async()/parameters_schema() 中 60% 重复代码
//   2. 统一 query/count 参数解析和范围限制
//   3. 统一 result_json 包装
//
// 子类只需实现：
//   - do_search(query, count) -> vector<WebSearchResult>
//   - get_tool_name() / get_description() / get_default_count() / get_max_count()

#include <agent/i_tool.h>
#include "web_search_common.h"
#include <string>
#include <vector>
#include <functional>

namespace agent {

class WebSearchToolBase : public ITool {
public:
    u8str execute(const u8str& arguments) override final;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override { return false; }

protected:
    // === 子类必须实现的钩子 ===
    virtual std::vector<WebSearchResult> do_search(const std::string& query, int count) = 0;
    virtual u8str get_tool_name() const = 0;
    virtual u8str get_tool_description() const = 0;
    virtual int    get_default_count() const { return 5; }
    virtual int    get_max_count() const { return 20; }

    // === 可选重写：自定义失败消息 ===
    virtual std::string get_failure_message() const {
        return "Web search failed. Check API key and network connectivity.";
    }
};

}  // namespace agent
