#include "HapticLog.h"

HapticLog::HapticLog(std::size_t capacity)
: capacity_{capacity} {
    if (capacity_ == 0) {
        capacity_ = 1;
    }
}

void HapticLog::push(const HapticEventLogEntry& entry) {
    buffer_.push_back(entry);
    while (buffer_.size() > capacity_) {
        buffer_.pop_front();
    }
}

void HapticLog::clear() noexcept {
    buffer_.clear();
}

const std::deque<HapticEventLogEntry>& HapticLog::entries() const noexcept {
    return buffer_;
}

std::size_t HapticLog::capacity() const noexcept {
    return capacity_;
}
