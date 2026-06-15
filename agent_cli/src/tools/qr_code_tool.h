#pragma once

#include <agent/i_tool.h>

namespace qrcodegen {
class QrCode;
}

namespace agent_cli {

class QrCodeTool : public agent::ITool {
public:
    agent::u8str name() const override;
    agent::u8str description() const override;
    agent::u8str parameters_schema() const override;
    agent::u8str execute(const agent::u8str& arguments) override;
    void execute_async(const agent::u8str& arguments, std::function<void(agent::u8str)> callback) override;
    bool requires_confirmation() const override;

    // 将二维码打印到控制台（无静区边框，直接逐行输出）
    static void print_to_console(const qrcodegen::QrCode& qr);
};

} // namespace agent_cli