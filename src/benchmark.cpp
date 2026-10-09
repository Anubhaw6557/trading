#include "matching_engine.h"
#include "order.h"
#include "report.h"
#include "metrics.h"
#include "mpsc_queue.h"
#include "spsc_queue.h"
#include "latency.h"
#include "rdtsc.h"
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <algorithm>
#include <iomanip>
#include <cstring>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace {

struct SymbolSpec {
    const char name[8];
    int base_price;
};

const SymbolSpec SYMBOLS[5] = {
    {"A", 15000},
    {"B", 280000},
    {"C", 30000},
    {"D", 320000},
    {"E", 25000}
};

struct PnLEntry {
    int64_t cash = 0;
    int64_t pos = 0;
};

struct SimpleLatencyRecorder {
    std::vector<uint64_t> samples;
    void add(uint64_t cycles) { samples.push_back(cycles); }

    void report(const char* name) {
        if (samples.empty()) {
            std::cout << name << "\n\n";
            std::cout << "p50: 0 ns\n\n";
            std::cout << "p99: 0 ns\n\n";
            std::cout << "max: 0 ns\n\n";
            return;
        }
        std::sort(samples.begin(), samples.end());
        const auto& clock = TscClock::instance();
        auto to_ns = [&](uint64_t cycles) { return clock.cycles_to_ns(cycles); };

        uint64_t p50 = to_ns(samples[samples.size() * 50 / 100]);
        uint64_t p99 = to_ns(samples[samples.size() * 99 / 100]);
        uint64_t max_val = to_ns(samples.back());

        std::cout << name << "\n\n";
        std::cout << "p50: " << p50 << " ns\n\n";
        std::cout << "p99: " << p99 << " ns\n\n";
        std::cout << "max: " << max_val << " ns\n\n";
    }
};

bool pin_current_thread(size_t logical_cpu) {
#if defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) return false;

    size_t available_count = 0;
    for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &allowed)) ++available_count;
    }
    if (available_count == 0) return false;

    const size_t target = logical_cpu % available_count;
    size_t current = 0;
    unsigned selected_cpu = 0;
    for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &allowed)) continue;
        if (current++ == target) {
            selected_cpu = cpu;
            break;
        }
    }

    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    CPU_SET(selected_cpu, &affinity);
    return pthread_setaffinity_np(
        pthread_self(), sizeof(affinity), &affinity) == 0;
#else
    (void)logical_cpu;
    return false;
#endif
}

} // namespace

int main(int argc, char** argv) {
    uint64_t total_orders = 1000000;
    uint64_t warmup_orders = 100000;
    size_t num_producers = 4;
    size_t num_symbols = 5;
    size_t num_traders = 3;

    if (argc > 1) total_orders = std::stoull(argv[1]);
    if (argc > 2) warmup_orders = std::stoull(argv[2]);
    if (argc > 3) num_producers = std::stoul(argv[3]);
    if (argc > 4) num_symbols = std::min<size_t>(std::stoul(argv[4]), 5);
    if (argc > 5) num_traders = std::stoul(argv[5]);

    TscClock::instance().calibrate();

    constexpr size_t Order_queue_size = 1 << 16;
    constexpr size_t Trade_queue_size = 1 << 16;
    constexpr size_t Report_queue_size = 1 << 16;
    constexpr size_t Pnl_queue_size = 1 << 16;

    MPSCQueue<Order> order_q(Order_queue_size);
    SPSCQueue<Trade> trade_q(Trade_queue_size);
    MPSCQueue<ExecutionReport> report_q(Report_queue_size);
    MPSCQueue<Pnlrequest> pnl_q(Pnl_queue_size);

    MatchingEngine engine(order_q, trade_q, report_q);
    PerformanceMetrics metrics;

    std::atomic<uint64_t> total_trades{0};
    std::unordered_map<int, std::unordered_map<std::string, PnLEntry>> pnl_map;
    SimpleLatencyRecorder pnl_lat;

    std::atomic<uint64_t> warmup_reports_seen{0};
    std::atomic<bool> in_warmup{true};
    std::atomic<uint64_t> profile_start_cycles{0};
    std::atomic<uint64_t> producer_end_cycles{0};
    std::atomic<uint64_t> matcher_end_cycles{0};
    std::atomic<uint64_t> pnl_end_cycles{0};
    std::atomic<uint64_t> report_end_cycles{0};
    std::vector<uint64_t> producer_full_retries(num_producers, 0);

    // Helper lambda for producer thread order generation
    auto run_producer_batch = [&](uint64_t count, size_t producer_id, uint64_t id_base) {
        pin_current_thread(producer_id + 1);
        uint64_t next_order_id = id_base;
        std::mt19937_64 rng(42 + producer_id * 1000);
        std::uniform_int_distribution<int> price_offset(-15, 15);
        std::uniform_int_distribution<int> qty_dist(10, 500);

        for (uint64_t i = 0; i < count; ++i) {
            uint64_t oid = next_order_id++;
            int trader_id = static_cast<int>(rng() % num_traders + 1);
            size_t sym_idx = rng() % num_symbols;
            const auto& sym = SYMBOLS[sym_idx];
            Side side = (rng() % 2 == 0) ? Side::BUY : Side::SELL;

            Order o{};
            o.id = oid;
            o.trader_id = trader_id;
            o.rtype = RequestType::NEW;
            std::memcpy(o.symbol, sym.name, 8);
            o.side = side;
            o.type = OrderType::LIMIT;
            o.price = sym.base_price + price_offset(rng);
            o.qty = qty_dist(rng);
            o.t_created = now_tsc();

            while (!order_q.push(o)) {
                ++producer_full_retries[producer_id];
            }
        }
    };

    // 1. Multi-Producer Manager Thread
    std::thread gen_manager([&]() {
        // --- PHASE 1: MULTI-PRODUCER WARMUP ---
        uint64_t warmup_per_producer = warmup_orders / num_producers;
        std::vector<std::thread> warmup_producers;
        warmup_producers.reserve(num_producers);
        for (size_t p = 0; p < num_producers; ++p) {
            const uint64_t id_base = p * warmup_per_producer + 1;
            warmup_producers.emplace_back(run_producer_batch, warmup_per_producer, p, id_base);
        }
        for (auto& t : warmup_producers) {
            t.join();
        }

        // Wait for all warmup orders to be acknowledged
        while (warmup_reports_seen.load() < warmup_per_producer * num_producers) {
            std::this_thread::yield();
        }

        // --- RESET POINT BEFORE PROFILING ---
        in_warmup.store(false);
        engine.ingress_lat.v.clear();
        engine.match_lat.v.clear();
        pnl_lat.samples.clear();
        total_trades.store(0);

        order_q.queue_spins_in.store(0);
        order_q.queue_spins_out.store(0);
        trade_q.queue_spins_in.store(0);
        trade_q.queue_spins_out.store(0);
        report_q.queue_spins_in.store(0);
        report_q.queue_spins_out.store(0);
        std::fill(producer_full_retries.begin(), producer_full_retries.end(), 0);

        std::cout << "[Warmup Complete: " << warmup_per_producer * num_producers 
                  << " orders across " << num_producers << " concurrent producer threads.]\n"
                  << "[Starting True MPSC Multi-Producer Profiling...]\n\n";

        // --- PHASE 2: MULTI-PRODUCER PROFILING ---
        profile_start_cycles.store(now_tsc(), std::memory_order_release);
        uint64_t profile_per_producer = total_orders / num_producers;
        std::vector<std::thread> profile_producers;
        profile_producers.reserve(num_producers);
        for (size_t p = 0; p < num_producers; ++p) {
            const uint64_t id_base = warmup_per_producer * num_producers
                + p * profile_per_producer + 1;
            profile_producers.emplace_back(run_producer_batch, profile_per_producer, p, id_base);
        }
        for (auto& t : profile_producers) {
            t.join();
        }
        uint64_t total_full_retries = 0;
        for (uint64_t local_full_retries : producer_full_retries) {
            total_full_retries += local_full_retries;
        }
        order_q.queue_spins_in.store(total_full_retries, std::memory_order_relaxed);
        producer_end_cycles.store(now_tsc(), std::memory_order_release);
    });

    // 2. Matching Engine Thread (Consumer)
    std::thread match_thread([&]() {
        pin_current_thread(0);
        engine.run();
        matcher_end_cycles.store(now_tsc(), std::memory_order_release);
    });

    // 3. Custom PnL Processor Thread
    std::thread pnl_processor_thread([&]() {
        Trade t;
        Pnlrequest req;
        while (true) {
            bool work = false;
            if (trade_q.pop(t)) {
                work = true;
                if (t.trade_buyer_id == -1 || t.trade_seller_id == -1) break;
                std::string sym = get_symbol(t.symbol);
                auto& buyer = pnl_map[t.trade_buyer_id][sym];
                auto& seller = pnl_map[t.trade_seller_id][sym];
                buyer.cash -= int64_t(t.price) * t.qty;
                buyer.pos += t.qty;
                seller.cash += int64_t(t.price) * t.qty;
                seller.pos -= t.qty;
                t.t_emitted = now_tsc();
                if (!in_warmup.load() && t.t_emitted > t.t_created) {
                    pnl_lat.add(t.t_emitted - t.t_created);
                    ++total_trades;
                }
            } else if (!in_warmup.load()) {
                ++trade_q.queue_spins_out;
            }
            if (pnl_q.pop(req)) {
                work = true;
            }
            if (!work) std::this_thread::yield();
        }
        pnl_end_cycles.store(now_tsc(), std::memory_order_release);
    });

    // 4. Report Collector Thread
    std::thread report_thread([&]() {
        ExecutionReport r;
        uint64_t warmup_target = (warmup_orders / num_producers) * num_producers;
        while (true) {
            while (!report_q.pop(r)) {
                if (!in_warmup.load()) {
                    ++report_q.queue_spins_out;
                }
                std::this_thread::yield();
            }
            if (r.trader_id == -1) break;

            if (r.type == ExecType::ACK) {
                if (r.id <= warmup_target) {
                    warmup_reports_seen.fetch_add(1);
                }
            }

            if (!in_warmup.load()) {
                metrics.record(r.type);
            }
        }
        report_end_cycles.store(now_tsc(), std::memory_order_release);
    });

    // Wait for all producer threads to finish
    gen_manager.join();

    // Shutdown matching engine
    Order shutdown{};
    shutdown.type = OrderType::SHUTDOWN;
    while (!order_q.push(shutdown)) {
        ++order_q.queue_spins_in;
    }

    match_thread.join();
    pnl_processor_thread.join();
    report_thread.join();

    const uint64_t profile_start = profile_start_cycles.load(std::memory_order_acquire);
    const uint64_t end_cycles = now_tsc();
    const uint64_t total_ns = TscClock::instance().cycles_to_ns(end_cycles - profile_start);
    auto stage_ns = [profile_start](uint64_t end) {
        return TscClock::instance().cycles_to_ns(end - profile_start);
    };

    // Print in exact README.md format
    std::cout << "Execution Summary\n\n";
    std::cout << "Total Orders Executed: " << total_orders << "\n\n";
    std::cout << "Total Trades Executed: " << total_trades.load() << "\n\n";

    std::cout << "Profit & Loss (PnL)\n";
    for (size_t tr = 1; tr <= num_traders; ++tr) {
        std::cout << "Trader " << tr << "\n\n";
        for (size_t s = 0; s < num_symbols; ++s) {
            std::string sym_name = SYMBOLS[s].name;
            int64_t cash = pnl_map[tr][sym_name].cash;
            int64_t pos = pnl_map[tr][sym_name].pos;
            std::cout << sym_name << " — Cash: " << cash << " | Position: " << pos << "\n\n";
        }
    }

    std::cout << "Latency Metrics (nanoseconds)\n";

    SimpleLatencyRecorder ingress_rec, match_rec;
    for (auto c : engine.ingress_lat.v) ingress_rec.add(c);
    for (auto c : engine.match_lat.v) match_rec.add(c);

    ingress_rec.report("Ingress (Market Replay → Order Queue)");
    match_rec.report("Matching Engine");
    pnl_lat.report("Metrics / PnL Updates");

    std::cout << "--- Queue Spin Statistics ---\n\n";
    std::cout << "Order Queue\n";
    std::cout << "  Producer spins (push):  " << order_q.queue_spins_in.load() << "\n";
    std::cout << "  Consumer spins (pop):   " << order_q.queue_spins_out.load() << "\n\n";
    std::cout << "Trade Queue\n";
    std::cout << "  Producer spins (push):  " << trade_q.queue_spins_in.load() << "\n";
    std::cout << "  Consumer spins (pop): " << trade_q.queue_spins_out.load() << "\n\n";

    std::cout << "Warmup orders excluded from timing: "
              << (warmup_orders / num_producers) * num_producers << "\n\n";
    std::cout << "Steady-state profiling time: " << total_ns << " ns\n\n\n";
    std::cout << "Stage timing from profiling start\n\n";
    std::cout << "  Producers complete: "
              << stage_ns(producer_end_cycles.load(std::memory_order_acquire)) << " ns\n";
    std::cout << "  Matcher complete:   "
              << stage_ns(matcher_end_cycles.load(std::memory_order_acquire)) << " ns\n";
    std::cout << "  PnL complete:       "
              << stage_ns(pnl_end_cycles.load(std::memory_order_acquire)) << " ns\n";
    std::cout << "  Reports complete:   "
              << stage_ns(report_end_cycles.load(std::memory_order_acquire)) << " ns\n\n";
    double throughput = (double)total_orders * 1e9 / (double)total_ns;
    std::cout << std::fixed << std::setprecision(5);
    std::cout << "Throughput for orders : " << throughput << "\n\n";

    return 0;
}
