#pragma once

#include <agent/i_memory.h>
#include <map>
#include <mutex>

namespace agent {

// 内置默认 Memory 实现（简单的 KV 存储）
class DefaultMemory : public IMemory {
public:
    u8str get_memory_name() const override;
    void store(const u8str& key, const u8str& value) override;
    std::optional<u8str> retrieve(const u8str& key) const override;
    std::vector<u8str> search(const u8str& query) const override;
    void remove(const u8str& key) override;
    void clear() override;

private:
    mutable std::mutex          mutex_;
    std::map<u8str, u8str>      data_;
};

} // namespace agent
