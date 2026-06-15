# OpenSERP

搜索引擎聚合器，支持多种搜索引擎，作为本地 HTTP 搜索服务运行。

## 安装步骤

### 1. 下载程序

到 GitHub Releases 页面下载对应平台的程序：
https://github.com/karust/openserp/releases

### 2. 放置文件

下载的程序解压后得到 `openserp.exe`，放到 `third_party/openserp/` 目录即可。

构建脚本（`build.ps1` / `build.bat`）会自动将此文件拷贝到构建产物目录。

## 使用方式

OpenSERP 作为本地 HTTP 服务运行，Agent 通过 `openserp_search` 工具调用。

### 环境变量

| 变量 | 说明 | 示例 |
|------|------|------|
| `OPENSERP_SEARCH_ENGINES` | 搜索引擎列表（逗号分隔） | `bing` |
| `OPENSERP_PATH` | openserp.exe 路径 | `D:\tools\openserp.exe` |
| `OPENSERP_PORT` | 服务端口（默认 7070） | `7070` |

设置 `OPENSERP_SEARCH_ENGINES` 环境变量后，`openserp_search` 工具会自动注册。
