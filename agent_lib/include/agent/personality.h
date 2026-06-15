#pragma once
#include "types.h"
#include <map>

namespace agent {

/**
 * @brief Agent人格文档结构体
 *
 * 定义Agent的人格特征、身份、行为规范等文档信息。
 * 用于构建System Prompt，影响Agent的行为和回答方式。
 */
struct PersonalityDocs {
    u8str                   soul;          // SOUL.md，Agent人格描述（最大200字符）
    u8str                   identity;      // IDENTITY.md，Agent身份名片，包含ID、姓名、自我介绍（最大200字符）
    u8str                   agents;        // AGENTS.md，Agent行为规范/原则/道德/价值观/底线（最大2000字符）
    u8str                   skill_doc;     // 可用的 Skill 描述（内部自动生成）
    u8str                   user_index;    // USER.md，Agent/人类认知索引，索引具体的USER-name.md
    std::map<u8str, u8str>  user_profiles; // USER-name.md，对某个具体Agent/人类的认知（每个最大500字符）
};

} // namespace agent
