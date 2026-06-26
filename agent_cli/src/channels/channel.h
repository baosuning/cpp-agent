#pragma once
// agent_cli/src/channels/channel.h
// 统一渠道接口：所有消息平台（微信/飞书/企业微信/QQ 等）实现此接口
// Agent 通过此接口与外部消息平台解耦，新增平台只需实现 IChannel + 注册到工厂

#include <agent/agent.h>

namespace agent_cli::channels {

// 消息渠道接口
// 职责：管理平台连接（登录/鉴权）、接收消息、转发给 Agent、将 Agent 输出回复到平台
class IChannel {
public:
    virtual ~IChannel() = default;

    // 启动渠道：执行登录/连接，成功后启动消息收发线程
    // agent 已由调用方创建并配置好工具/MCP/记忆等
    virtual bool start(agent::Agent& agent) = 0;

    // 停止渠道：停止线程、断开连接、保存持久化状态
    virtual void stop() = 0;

    // 是否运行中
    virtual bool is_running() const = 0;
};

} // namespace agent_cli::channels
