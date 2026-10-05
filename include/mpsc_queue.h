#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

template <typename T>
class MPSCQueue {
public:
    explicit MPSCQueue(size_t capacity)
        : capacity_(capacity), buffer_(std::make_unique<Cell[]>(capacity)) {
        assert(capacity_ > 0);
        assert((capacity_ & (capacity_ - 1)) == 0 && "capacity must be a power of two");
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    bool push(const T& item) {
        Cell* cell;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & (capacity_ - 1)];
            const size_t sequence = cell->sequence.load(std::memory_order_acquire);
            const intptr_t difference = static_cast<intptr_t>(sequence) - static_cast<intptr_t>(pos);
            if (difference == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) break;
            } else if (difference < 0) {
                return false;
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
        cell->value = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        const size_t pos = dequeue_pos_;
        Cell& cell = buffer_[pos & (capacity_ - 1)];
        if (cell.sequence.load(std::memory_order_acquire) != pos + 1) return false;
        item = cell.value;
        cell.sequence.store(pos + capacity_, std::memory_order_release);
        dequeue_pos_ = pos + 1;
        return true;
    }

    size_t capacity() const noexcept { return capacity_; }

    std::atomic<uint64_t> queue_spins_in{0};
    std::atomic<uint64_t> queue_spins_out{0};

private:
    struct Cell {
        std::atomic<size_t> sequence{0};
        T value{};
    };

    const size_t capacity_;
    std::unique_ptr<Cell[]> buffer_;
    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) size_t dequeue_pos_{0};
};
