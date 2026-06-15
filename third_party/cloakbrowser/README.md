# CloakBrowser

基于 Chromium 的浏览器自动化工具，通过 `cloakbrowsermcp` Python 模块提供 MCP Server 接口。

## 安装步骤

### 1. 安装 cloakbrowsermcp 服务器程序

```bash
pip install cloakbrowsermcp -i https://pypi.tuna.tsinghua.edu.cn/simple
```

### 2. 下载 CloakBrowser 浏览器内核

**方式一：自动下载**

```bash
set CLOAKBROWSER_CACHE_DIR=D:\cloakbrowser_cache
python -m cloakbrowser install
```

**方式二：手动下载**

到 GitHub Releases 页面下载并解压到指定目录：
https://github.com/CloakHQ/CloakBrowser/releases

### 3. 配置 MCP

在 `agent_cli/config/mcps/mcp.json` 中填入解压后的 `chrome.exe` 路径：

```json
{
  "mcpServers": {
    "cloakbrowsermcp": {
      "command": "python",
      "args": ["-m", "cloakbrowsermcp"],
      "timeout": 120000,
      "env": {
        "CLOAKBROWSER_BINARY_PATH": "D:\\cloakbrowser_cache\\chrome.exe"
      }
    }
  }
}
```

## 提供的工具

- `cloak_launch` — 启动浏览器实例
- `cloak_navigate` — 导航到 URL
- `cloak_click` — 点击元素
- `cloak_type` — 输入文本
- `cloak_snapshot` — 获取页面快照
