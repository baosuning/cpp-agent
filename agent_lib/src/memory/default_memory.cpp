#include "default_memory.h"

namespace agent {

u8str DefaultMemory::get_memory_name() const {
    return u8str(u8"default_memory");
}

void DefaultMemory::store(const u8str& key, const u8str& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_[key] = value;
}

std::optional<u8str> DefaultMemory::retrieve(const u8str& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = data_.find(key);
    if (it != data_.end()) return it->second;
    return std::nullopt;
}

std::vector<u8str> DefaultMemory::search(const u8str& query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<u8str> results;
    for (const auto& [k, v] : data_) {
        if (k.find(query) != u8str::npos || v.find(query) != u8str::npos)
            results.push_back(v);
    }
    return results;
}

void DefaultMemory::remove(const u8str& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.erase(key);
}

void DefaultMemory::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
}

} // namespace agent
