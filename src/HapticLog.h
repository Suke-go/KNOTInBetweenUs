#pragma once

#include <cstdint>
#include <deque>

struct HapticEventLogEntry {
    std::uint64_t beatId = 0;
    float intensity = 0.0f;
    std::uint32_t holdMs = 0U;
    double createdAtSec = 0.0;
};

class HapticLog {
public:
    explicit HapticLog(std::size_t capacity = 128);

    void push(const HapticEventLogEntry& entry);
    void clear() noexcept;

    [[nodiscard]] const std::deque<HapticEventLogEntry>& entries() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    std::size_t capacity_ = 0;
    std::deque<HapticEventLogEntry> buffer_;
};
