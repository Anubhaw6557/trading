#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <cassert>

template <typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity) : capacity_(capacity), buffer_(std::make_unique<T[]>(capacity_)) {
        assert(capacity > 0);
        assert((capacity & (capacity - 1)) == 0 && "capacity must be a power of two");
    }
    ~SPSCQueue() = default;
    bool push(const T& item) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next = increment(tail);
        if (next == head_.load(std::memory_order_acquire)) return false;
        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }
    bool pop(T& item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        item = buffer_[head];
        head_.store(increment(head), std::memory_order_release);
        return true;
    }
    size_t capacity() const { return capacity_ - 1; }

    alignas(64) std::atomic<uint64_t> queue_spins_in{0};
    alignas(64) std::atomic<uint64_t> queue_spins_out{0};

private:
    size_t increment(size_t idx) const noexcept {
        return (idx + 1) & (capacity_ - 1);
    }
    const size_t capacity_;
    const std::unique_ptr<T[]> buffer_;

    // Producer and consumer on separate cache lines to prevent false sharing
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) std::atomic<size_t> head_{0};
};
