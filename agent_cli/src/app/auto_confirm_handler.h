#pragma once
// agent_cli/src/app/auto_confirm_handler.h
// 自动确认处理器：用于 channel 模式等无 stdin 交互场景，
// 避免工具调用因等待用户确认而永久阻塞。

#include <agent/i_user_confirm_handler.h>

namespace agent_cli {

class AutoConfirmHandler : public agent::IUserConfirmHandler {
public:
    AutoConfirmHandler() = default;
    ~AutoConfirmHandler() override = default;

    agent::ConfirmResult confirm(const agent::ConfirmRequest& request) override;
    void confirm_async(const agent::ConfirmRequest& request,
                       std::function<void(agent::ConfirmResult)> callback) override;
};

} // namespace agent_cli
