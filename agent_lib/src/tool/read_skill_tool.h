#pragma once
#include <agent/i_tool.h>
#include <memory>

namespace agent {

class SkillScanner;

class ReadSkillTool : public ITool {
public:
    explicit ReadSkillTool(std::shared_ptr<SkillScanner> scanner);

    u8str name() const override;
    u8str description() const override;
    u8str parameters_schema() const override;
    u8str execute(const u8str& arguments) override;
    void execute_async(const u8str& arguments, std::function<void(u8str)> callback) override;
    bool requires_confirmation() const override;

private:
    std::shared_ptr<SkillScanner> scanner_;
};

} // namespace agent