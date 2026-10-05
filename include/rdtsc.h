#pragma once

#include <cstdint>
#include <chrono>
#include <thread>
#include <x86intrin.h>

class TscClock {
public:
    static TscClock& instance() {
        static TscClock clock;
        return clock;
    }

    void calibrate() {
        constexpr int samples = 5;
        double total = 0.0;
        for (int i = 0; i < samples; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            const uint64_t c0 = rdtscp();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const auto t1 = std::chrono::steady_clock::now();
            const uint64_t c1 = rdtscp();
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            const uint64_t cycles = c1 - c0;
            if (cycles > 0) total += static_cast<double>(ns) / static_cast<double>(cycles);
        }
        ns_per_cycle_ = total / samples;
        start_cycles_ = rdtscp();
        start_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    uint64_t now_cycles() const {
        return rdtscp();
    }

    uint64_t cycles_to_ns(uint64_t cycles) const {
        return static_cast<uint64_t>(static_cast<double>(cycles) * ns_per_cycle_);
    }

    uint64_t elapsed_ns(uint64_t start_cycles) const {
        return cycles_to_ns(rdtscp() - start_cycles);
    }

    double ns_per_cycle() const { return ns_per_cycle_; }

private:
    TscClock() = default;

    static uint64_t rdtscp() {
        unsigned int aux;
        return __rdtscp(&aux);
    }

    double ns_per_cycle_ = 1.0;
    uint64_t start_cycles_ = 0;
    uint64_t start_ns_ = 0;
};

inline uint64_t now_tsc() {
    return TscClock::instance().now_cycles();
}
