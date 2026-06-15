#pragma once

#include <agent/i_memory.h>
#include <map>
#include <mutex>

namespace agent_cli {

class SimpleMemory : public agent::IMemory {
public:
    agent::u8str get_memory_name() const override;
    void store(const agent::u8str& key, const agent::u8str& value) override;
    std::optional<agent::u8str> retrieve(const agent::u8str& key) const override;
    std::vector<agent::u8str> search(const agent::u8str& query) const override;
    void remove(const agent::u8str& key) override;
    void clear() override;

private:
    mutable std::mutex mutex_;
    std::map<agent::u8str, agent::u8str> data_;
};

} // namespace agent_cli