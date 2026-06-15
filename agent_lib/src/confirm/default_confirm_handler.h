#pragma once
#include <agent/i_user_confirm_handler.h>
#include <iostream>
#include <string>

namespace agent {

class DefaultConfirmHandler : public IUserConfirmHandler {
public:
    DefaultConfirmHandler() = default;
    ~DefaultConfirmHandler() override = default;

    ConfirmResult confirm(const ConfirmRequest& request) override;
    void confirm_async(const ConfirmRequest& request,
                       std::function<void(ConfirmResult)> callback) override;
};

} // namespace agent
