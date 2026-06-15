#pragma once
#include "types.h"
#include <memory>
#include <functional>

namespace agent {

class IUserConfirmHandler {
public:
    virtual ~IUserConfirmHandler() = default;

    virtual ConfirmResult confirm(const ConfirmRequest& request) = 0;
    virtual void confirm_async(
        const ConfirmRequest& request,
        std::function<void(ConfirmResult)> callback) = 0;
};

using UserConfirmHandlerPtr = std::shared_ptr<IUserConfirmHandler>;

} // namespace agent
