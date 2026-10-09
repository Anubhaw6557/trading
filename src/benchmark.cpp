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

} // namespace

int main(int argc, char** argv) {
    uint64_t total_orders = 1000000;
    uint64_t warmup_orders = 100000;
    size_t num_symbols = 5;
    size_t num_traders = 3;

    if (argc > 1) total_orders = std::stoull(argv[1]);
    if (argc > 2) warmup_orders = std::stoull(argv[2]);
    if (argc > 3) num_symbols = std::min<size_t>(std::stoul(argv[3]), 5);
    if (argc > 4) num_traders = std::stoul(argv[4]);

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

    // 1. Order Generator Thread
    std::thread gen_thread([&]() {
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<int> price_offset(-15, 15);
        std::uniform_int_distribution<int> qty_dist(10, 500);

        // --- PHASE 1: WARMUP ORDERS ---
        for (uint64_t i = 1; i <= warmup_orders; ++i) {
            int trader_id = static_cast<int>(rng() % num_traders + 1);
            size_t sym_idx = rng() % num_symbols;
            const auto& sym = SYMBOLS[sym_idx];
            Side side = (rng() % 2 == 0) ? Side::BUY : Side::SELL;

            Order o{};
            o.id = i;
            o.trader_id = trader_id;
            o.rtype = RequestType::NEW;
            std::memcpy(o.symbol, sym.name, 8);
            o.side = side;
            o.type = OrderType::LIMIT;
            o.price = sym.base_price + price_offset(rng);
            o.qty = qty_dist(rng);
            o.t_created = now_tsc();

            while (!order_q.push(o)) {
                ++order_q.queue_spins_in;
            }
        }

        // Wait for warmup orders to clear report queue
        while (warmup_reports_seen.load() < warmup_orders) {
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

        std::cout << "[Warmup Complete: " << warmup_orders << " orders. Starting Steady-State Profiling...]\n\n";

        // --- PHASE 2: REAL PROFILING ORDERS ---
        for (uint64_t i = warmup_orders + 1; i <= warmup_orders + total_orders; ++i) {
            int trader_id = static_cast<int>(rng() % num_traders + 1);
            size_t sym_idx = rng() % num_symbols;
            const auto& sym = SYMBOLS[sym_idx];
            Side side = (rng() % 2 == 0) ? Side::BUY : Side::SELL;

            Order o{};
            o.id = i;
            o.trader_id = trader_id;
            o.rtype = RequestType::NEW;
            std::memcpy(o.symbol, sym.name, 8);
            o.side = side;
            o.type = OrderType::LIMIT;
            o.price = sym.base_price + price_offset(rng);
            o.qty = qty_dist(rng);
            o.t_created = now_tsc();

            while (!order_q.push(o)) {
                ++order_q.queue_spins_in;
            }
        }
    });

    // 2. Matching Engine Thread
    std::thread match_thread([&]() {
        engine.run();
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
            }
            if (pnl_q.pop(req)) {
                work = true;
            }
            if (!work) std::this_thread::yield();
        }
    });

    // 4. Report Collector Thread
    std::thread report_thread([&]() {
        ExecutionReport r;
        while (true) {
            while (!report_q.pop(r)) {
                if (!in_warmup.load()) {
                    ++report_q.queue_spins_out;
                }
                std::this_thread::yield();
            }
            if (r.trader_id == -1) break;

            if (r.type == ExecType::ACK) {
                if (r.id <= warmup_orders) {
                    warmup_reports_seen.fetch_add(1);
                }
            }

            if (!in_warmup.load()) {
                metrics.record(r.type);
            }
        }
    });

    uint64_t start_cycles = now_tsc();

    // Wait for generator to finish pushing all orders
    gen_thread.join();

    // Shutdown matching engine
    Order shutdown{};
    shutdown.type = OrderType::SHUTDOWN;
    while (!order_q.push(shutdown)) {
        ++order_q.queue_spins_in;
    }

    match_thread.join();
    pnl_processor_thread.join();
    report_thread.join();

    uint64_t end_cycles = now_tsc();
    uint64_t total_ns = TscClock::instance().cycles_to_ns(end_cycles - start_cycles);

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

    std::cout << "Total time for execution : " << total_ns << " ns\n\n\n";
    double throughput = (double)total_orders * 1e9 / (double)total_ns;
    std::cout << std::fixed << std::setprecision(5);
    std::cout << "Throughput for orders : " << throughput << "\n\n";

    return 0;
}
