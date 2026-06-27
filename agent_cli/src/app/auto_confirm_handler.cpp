// agent_cli/src/app/auto_confirm_handler.cpp
// 自动确认处理器实现

#include "auto_confirm_handler.h"
#include <util/log.h>

namespace agent_cli {

agent::ConfirmResult AutoConfirmHandler::confirm(const agent::ConfirmRequest& request) {
    std::string action(reinterpret_cast<const char*>(request.action_description.data()),
                       request.action_description.size());
    AGENT_LOG_INFO("AutoConfirm") << "Auto-confirming action: " << action;
    agent::ConfirmResult result;
    result.confirmed = true;
    return result;
}

void AutoConfirmHandler::confirm_async(const agent::ConfirmRequest& request,
                                       std::function<void(agent::ConfirmResult)> callback) {
    if (callback) {
        callback(confirm(request));
    }
}

} // namespace agent_cli
