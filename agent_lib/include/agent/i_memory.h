#pragma once
#include "types.h"
#include <memory>
#include <optional>
#include <vector>

namespace agent {

class IMemory {
public:
    virtual ~IMemory() = default;

    virtual void store(const u8str& key, const u8str& value) = 0;
    virtual std::optional<u8str> retrieve(const u8str& key) const = 0;
    virtual std::vector<u8str> search(const u8str& query) const = 0;
    virtual void remove(const u8str& key) = 0;
    virtual void clear() = 0;
    virtual u8str get_memory_name() const = 0;
};

using MemoryPtr = std::shared_ptr<IMemory>;

} // namespace agent
