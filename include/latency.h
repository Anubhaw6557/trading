#pragma once

#include <vector>
#include <algorithm>
#include <iostream>
#include "rdtsc.h"

class LatencyStats {
public :
    std::vector<uint64_t> v;
    void add(uint64_t cycles){
        v.push_back(cycles);
    }
    void report(const char* name) {
        if (v.empty()) return;
        std::sort(v.begin(), v.end());
        const auto& clock = TscClock::instance();
        auto to_ns = [&](uint64_t cycles) { return clock.cycles_to_ns(cycles); };
        std::cout << name << " (RDTSCP)\n";
        std::cout << " p50=" << to_ns(v[v.size()/2]) << " ns\n";
        std::cout << " p99=" << to_ns(v[v.size()*99/100]) << " ns\n";
        std::cout << " max=" << to_ns(v.back()) << " ns\n";
        std::cout << "\n----------------------\n";
    }
};
