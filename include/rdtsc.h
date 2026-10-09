#pragma once

#include <cstdint>
#include <chrono>
#include <thread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <x86intrin.h>
#define ARCH_X86 1
#elif defined(__aarch64__)
#define ARCH_ARM64 1
#endif

class TscClock {
public:
    static TscClock& instance() {
        static TscClock clock;
        return clock;
    }

    void calibrate() {
#if defined(ARCH_X86)
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
#elif defined(ARCH_ARM64)
        uint64_t freq = arm64_cntfrq();
        if (freq > 0) {
            ns_per_cycle_ = 1e9 / static_cast<double>(freq);
        } else {
            ns_per_cycle_ = 1.0;
        }
#else
        ns_per_cycle_ = 1.0;
#endif
    }

    uint64_t now_cycles() const {
#if defined(ARCH_X86)
        return rdtscp();
#elif defined(ARCH_ARM64)
        return arm64_cntvct();
#else
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
    }

    uint64_t cycles_to_ns(uint64_t cycles) const {
        return static_cast<uint64_t>(static_cast<double>(cycles) * ns_per_cycle_);
    }

    uint64_t elapsed_ns(uint64_t start_cycles) const {
        return cycles_to_ns(now_cycles() - start_cycles);
    }

    double ns_per_cycle() const { return ns_per_cycle_; }

private:
    TscClock() = default;

#if defined(ARCH_X86)
    static uint64_t rdtscp() {
        unsigned int aux;
        return __rdtscp(&aux);
    }
#elif defined(ARCH_ARM64)
    static uint64_t arm64_cntvct() {
        uint64_t val;
        asm volatile("mrs %0, cntvct_el0" : "=r"(val));
        return val;
    }
    static uint64_t arm64_cntfrq() {
        uint64_t val;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
        return val;
    }
#endif

    double ns_per_cycle_ = 1.0;
};

inline uint64_t now_tsc() {
    return TscClock::instance().now_cycles();
}
