#pragma once

#include "report.h"
#include <atomic>
#include <cstdint>
#include <iostream>
#include <iomanip>

class PerformanceMetrics {
public:
    std::atomic<uint64_t> acks{0};
    std::atomic<uint64_t> fills{0};
    std::atomic<uint64_t> partial_fills{0};
    std::atomic<uint64_t> cancelled{0};
    std::atomic<uint64_t> rejects{0};
    std::atomic<uint64_t> modified{0};
    std::atomic<uint64_t> round_turns{0};
    std::atomic<uint64_t> client_push_spins{0};

    void record(ExecType type) {
        switch (type) {
            case ExecType::ACK: ++acks; break;
            case ExecType::FILL: 
                ++fills;
                round_turns += fills;
                break;
            case ExecType::PARTIAL_FILL: ++partial_fills; break;
            case ExecType::CANCELLED: ++cancelled; break;
            case ExecType::REJECT: ++rejects; break;
            case ExecType::MODIFIED: ++modified; break;
            default: break;
        }
    }

    MetricsSnapshot snapshot(uint64_t report_q_spins_in, uint64_t report_q_spins_out) const {
        MetricsSnapshot s;
        s.acks = acks.load(std::memory_order_relaxed);
        s.fills = fills.load(std::memory_order_relaxed);
        s.partial_fills = partial_fills.load(std::memory_order_relaxed);
        s.cancelled = cancelled.load(std::memory_order_relaxed);
        s.rejects = rejects.load(std::memory_order_relaxed);
        s.modified = modified.load(std::memory_order_relaxed);
        s.round_turns = round_turns.load(std::memory_order_relaxed);
        s.report_q_spins_in = report_q_spins_in;
        s.report_q_spins_out = report_q_spins_out;
        s.client_push_spins = client_push_spins.load(std::memory_order_relaxed);
        return s;
    }

    void report(uint64_t elapsed_ns) const {
        uint64_t rts = round_turns.load(std::memory_order_relaxed);
        rts = rts/2;
        const double rt_throughput = elapsed_ns > 0
            ? (static_cast<double>(rts) * 1e9) / static_cast<double>(elapsed_ns)
            : 0.0;

        std::cout << "\n--- Execution Report Stats ---\n";
        std::cout << " ACKs:           " << acks.load() << "\n";
        std::cout << " FILLs:          " << fills.load() << "\n";
        std::cout << " PARTIAL_FILLs:  " << partial_fills.load() << "\n";
        std::cout << " CANCELLED:      " << cancelled.load() << "\n";
        std::cout << " REJECTs:        " << rejects.load() << "\n";
        std::cout << " MODIFIED:       " << modified.load() << "\n";
        std::cout << " Round turns:    " << rts << "\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << " RT throughput:  " << rt_throughput << " RT/s\n";
        std::cout << " Client push spins: " << client_push_spins.load() << "\n";
        std::cout << "------------------------------\n";
    }
};
