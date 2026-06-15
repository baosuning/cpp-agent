#include "default_confirm_handler.h"

namespace agent {

ConfirmResult DefaultConfirmHandler::confirm(const ConfirmRequest& request) {
    std::string action(reinterpret_cast<const char*>(request.action_description.data()),
                       request.action_description.size());
    std::string details(reinterpret_cast<const char*>(request.details.data()),
                        request.details.size());

    std::cout << action << std::endl;
    if (!details.empty()) {
        std::cout << details << std::endl;
    }
    std::cout << "[Y/n] ";

    std::string input;
    std::getline(std::cin, input);

    ConfirmResult result;
    if (input == "Y" || input == "y" || input.empty()) {
        result.confirmed = true;
    } else {
        result.confirmed = false;
        result.feedback = u8str(reinterpret_cast<const char8_t*>(input.data()),
                                input.size());
    }
    return result;
}

void DefaultConfirmHandler::confirm_async(const ConfirmRequest& request,
                                          std::function<void(ConfirmResult)> callback) {
    // Safe synchronous implementation (no detach/blocking stdin-in-thread risk).
    // When true async is needed, refactor to capture shared_ptr<self>.
    callback(confirm(request));
}

} // namespace agent
