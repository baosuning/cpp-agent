#pragma once
#include "types.h"
#include <memory>
#include <functional>

namespace agent {

/**
 * @brief LLM访问抽象接口
 *
 * 将AI大模型的访问抽象为接口，外部程序可自定义实现。
 * 框架内置了OpenAI、Claude、Kimi、DeepSeek、GLM等Provider。
 * 用户也可通过实现此接口使用任意LLM API（REST、WebSocket、SDK等）。
 */
class ILlmProvider {
public:
    virtual ~ILlmProvider() = default;

    /**
     * @brief 同步发送LLM请求
     * @param request LLM请求，包含消息列表和模型配置
     * @return LLM响应，包含生成内容、工具调用等
     */
    virtual LlmResponse send_request(const LlmRequest& request) = 0;

    /**
     * @brief 异步发送LLM请求
     * @param request LLM请求
     * @param callback 回调函数，接收LLM响应
     */
    virtual void send_request_async(
        const LlmRequest& request,
        std::function<void(LlmResponse)> callback) = 0;

    /**
     * @brief 获取Provider名称
     * @return Provider名称字符串
     */
    virtual u8str get_provider_name() const = 0;

    /**
     * @brief 设置调试模式（默认空实现，子类可重写）
     * @param enable 是否启用调试输出
     */
    virtual void set_debug(bool enable) {}
};

/**
 * @brief LLM Provider智能指针类型
 */
using LlmProviderPtr = std::shared_ptr<ILlmProvider>;

} // namespace agent
