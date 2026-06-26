#pragma once
// agent_cli/src/channels/channel_factory.h
// 渠道工厂：根据平台名称创建对应的 IChannel 实例
// 新增平台时在此注册即可

#include "channel.h"
#include <util/i_http_client.h>
#include <memory>
#include <string>

namespace agent_cli::channels {

// 创建渠道实例
// platform: 平台标识，当前支持 "wechat"，预留 "feishu"/"wecom"/"qq"
// http:     HTTP 客户端（各平台复用）
// data_dir: 运行时数据根目录（如 "data/"），各平台在下属自己的子目录
// 返回 nullptr 表示不支持的平台
std::unique_ptr<IChannel> create_channel(const std::string& platform,
                                         agent::IHttpClient& http,
                                         const std::string& data_dir);

} // namespace agent_cli::channels
