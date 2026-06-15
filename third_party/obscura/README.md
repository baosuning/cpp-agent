# Obscura

Rust 编写的轻量级无头浏览器，支持 CDP 协议和 MCP Server 模式，用于 AI Agent 浏览器自动化。

## 安装步骤

### 1. 下载程序

到 GitHub Releases 页面下载对应平台的程序：
https://github.com/h4ckf0r0day/obscura/releases

### 2. 放置文件

下载的程序解压后得到 `obscura.exe` 和 `obscura-worker.exe` 两个文件，放到 `third_party/obscura/` 目录即可。

构建脚本（`build.ps1` / `build.bat`）会自动将这两个文件拷贝到构建产物目录。

### 3. 配置 MCP

`agent_cli/config/mcps/mcp.json` 中已预置配置：

```json
{
  "mcpServers": {
    "obscura": {
      "command": "obscura.exe",
      "args": ["mcp", "--stealth"],
      "timeout": 120000
    }
  }
}
```

## 提供的工具

- `browser_navigate` — 导航到 URL
- `browser_snapshot` — 获取页面快照（可访问性树）
- `browser_click` — 点击元素
- `browser_fill` — 填写输入框（先清空再输入）
- `browser_type` — 追加输入文本
- `browser_press_key` — 按键
- `browser_select_option` — 选择下拉选项
- `browser_evaluate` — 执行 JavaScript
- `browser_wait_for` — 等待元素出现
- `browser_network_requests` — 获取网络请求列表
- `browser_console_messages` — 获取控制台消息
- `browser_close` — 关闭浏览器
