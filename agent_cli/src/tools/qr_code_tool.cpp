#include "qr_code_tool.h"
#include "../utils/utils.h"
#include <qrcodegen/qrcodegen.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace agent_cli {

namespace {

using json = nlohmann::json;

// 安全的 JSON 序列化，遇到无效 UTF-8 字节时用 U+FFFD 替换而非抛出异常
static std::string safe_dump(const json& j) {
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

json parse_args(const agent::u8str& arguments) {
    try {
        return json::parse(u8tostr(arguments));
    } catch (...) {
        return json::object();
    }
}

std::string result_json(bool success, const std::string& data, const std::string& error = "") {
    json j;
    j["success"] = success;
    if (success) {
        j["result"] = data;
    } else {
        j["error"] = error;
    }
    return safe_dump(j);
}

qrcodegen::QrCode::Ecc parse_ecc(const std::string& ecc_str) {
    std::string upper = ecc_str;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper == "LOW")       return qrcodegen::QrCode::Ecc::LOW;
    if (upper == "QUARTILE")  return qrcodegen::QrCode::Ecc::QUARTILE;
    if (upper == "HIGH")      return qrcodegen::QrCode::Ecc::HIGH;
    return qrcodegen::QrCode::Ecc::MEDIUM;
}

// 将 QR 码转换为文本形式，使用 Unicode 全角方块
// 每个模块水平加倍（两个字符），以补偿终端字符的高宽比
std::string qr_to_text(const qrcodegen::QrCode& qr) {
    int size = qr.getSize();
    constexpr int border_modules = 2;
    int display_cols = (size + border_modules * 2) * 2;

    std::ostringstream ss;

    for (int i = 0; i < border_modules; i++) {
        ss << std::string(display_cols, ' ') << "\n";
    }

    for (int y = 0; y < size; y++) {
        ss << std::string(border_modules * 2, ' ');
        for (int x = 0; x < size; x++) {
            if (qr.getModule(x, y)) {
                // 暗色模块：全角方块 U+2588
                ss << "\xE2\x96\x88\xE2\x96\x88";
            } else {
                ss << "  ";
            }
        }
        ss << std::string(border_modules * 2, ' ');
        ss << "\n";
    }

    for (int i = 0; i < border_modules; i++) {
        ss << std::string(display_cols, ' ') << "\n";
    }

    return ss.str();
}

} // anonymous namespace

// ============================================================
// QrCodeTool
// ============================================================

agent::u8str QrCodeTool::name() const {
    return strtou8("generate_qrcode");
}

agent::u8str QrCodeTool::description() const {
    return strtou8(
        "Generate a QR code as text (using Unicode block characters). "
        "Takes a text string and optional error correction level, "
        "returns a visible QR code in text format with size and version info."
    );
}

agent::u8str QrCodeTool::parameters_schema() const {
    return strtou8(R"schema({
        "type": "object",
        "properties": {
            "text": {
                "type": "string",
                "description": "The text content to encode into the QR code (max ~2953 characters for binary data)"
            },
            "error_correction": {
                "type": "string",
                "enum": ["LOW", "MEDIUM", "QUARTILE", "HIGH"],
                "description": "Error correction level: LOW (7%), MEDIUM (15%), QUARTILE (25%), HIGH (30%). Default: MEDIUM",
                "default": "MEDIUM"
            }
        },
        "required": ["text"]
    })schema");
}

agent::u8str QrCodeTool::execute(const agent::u8str& arguments) {
    auto args = parse_args(arguments);

    auto text_it = args.find("text");
    if (text_it == args.end() || !text_it->is_string()) {
        return strtou8(result_json(false, "", "Missing required parameter: text"));
    }

    std::string text = text_it->get<std::string>();
    if (text.empty()) {
        return strtou8(result_json(false, "", "Text content cannot be empty"));
    }

    qrcodegen::QrCode::Ecc ecc = qrcodegen::QrCode::Ecc::MEDIUM;
    auto ecc_it = args.find("error_correction");
    if (ecc_it != args.end() && ecc_it->is_string()) {
        ecc = parse_ecc(ecc_it->get<std::string>());
    }

    try {
        auto qr = qrcodegen::QrCode::encodeText(text.c_str(), ecc);

        // 打印二维码到控制台
        print_to_console(qr);

        std::string qr_text = qr_to_text(qr);

        json result;
        result["qr_code"] = qr_text;
        result["size"] = qr.getSize();
        result["version"] = qr.getVersion();
        result["printed_to_console"] = true;
        switch (qr.getErrorCorrectionLevel()) {
            case qrcodegen::QrCode::Ecc::LOW:      result["error_correction"] = "LOW"; break;
            case qrcodegen::QrCode::Ecc::MEDIUM:   result["error_correction"] = "MEDIUM"; break;
            case qrcodegen::QrCode::Ecc::QUARTILE: result["error_correction"] = "QUARTILE"; break;
            case qrcodegen::QrCode::Ecc::HIGH:     result["error_correction"] = "HIGH"; break;
        }

        return strtou8(result_json(true, safe_dump(result)));
    } catch (const std::length_error& e) {
        return strtou8(result_json(false, "", std::string("Data too long for QR code: ") + e.what()));
    } catch (const std::exception& e) {
        return strtou8(result_json(false, "", std::string("Failed to generate QR code: ") + e.what()));
    }
}

void QrCodeTool::execute_async(const agent::u8str& arguments, std::function<void(agent::u8str)> callback) {
    callback(execute(arguments));
}

bool QrCodeTool::requires_confirmation() const {
    return false;
}

void QrCodeTool::print_to_console(const qrcodegen::QrCode& qr) {
    int size = qr.getSize();
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            std::cout << (qr.getModule(x, y) ? "\xE2\x96\x88\xE2\x96\x88" : "  ");
        }
        std::cout << std::endl;
    }
}

} // namespace agent_cli