#include "simple_memory.h"
#include "utils/utils.h"

namespace agent_cli {

agent::u8str SimpleMemory::get_memory_name() const {
    return strtou8("simple_memory");
}

void SimpleMemory::store(const agent::u8str& key, const agent::u8str& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_[key] = value;
}

std::optional<agent::u8str> SimpleMemory::retrieve(const agent::u8str& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);
    if (it != data_.end()) return it->second;
    return std::nullopt;
}

std::vector<agent::u8str> SimpleMemory::search(const agent::u8str& query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<agent::u8str> results;
    for (const auto& [k, v] : data_) {
        if (k.find(query) != agent::u8str::npos || v.find(query) != agent::u8str::npos) {
            results.push_back(v);
        }
    }
    return results;
}

void SimpleMemory::remove(const agent::u8str& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.erase(key);
}

void SimpleMemory::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
}

} // namespace agent_cli