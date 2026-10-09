#pragma once

#include "spsc_queue.h"
#include "mpsc_queue.h"
#include "order_book.h"
#include "latency.h"
#include "report.h"
#include <unordered_map>
#include <string>

class MatchingEngine {
public:
    MatchingEngine(MPSCQueue<Order>& oq, SPSCQueue<Trade>& tq, MPSCQueue<ExecutionReport>& rq)
        : trade_q_(tq), report_q_(rq), order_q_(oq), node_pool_(200000) {}
    void run();
    LatencyStats ingress_lat;
    LatencyStats match_lat;
    SPSCQueue<Trade>& trade_q_;
    MPSCQueue<ExecutionReport>& report_q_;
private:
    MPSCQueue<Order>& order_q_;
    ObjectPool<OrderNode> node_pool_;
    std::unordered_map<std::string, OrderBook> books_;

    OrderBook& get_book(const char symbol[8]) {
        std::string key(symbol, strnlen(symbol, 8));
        auto it = books_.find(key);
        if (it == books_.end()) {
            auto [inserted, ok] = books_.emplace(key, OrderBook(&node_pool_));
            return inserted->second;
        }
        return it->second;
    }
};
