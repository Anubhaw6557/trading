#pragma once

#include "spsc_queue.h"
#include "mpsc_queue.h"
#include "order_book.h"
#include "latency.h"
#include "report.h"
#include <unordered_map>

class MatchingEngine {
public:
    MatchingEngine(MPSCQueue<Order>& oq,SPSCQueue<Trade>& tq,MPSCQueue<ExecutionReport>& rq): order_q_(oq), trade_q_(tq),report_q_(rq) {}
    void run();
    LatencyStats ingress_lat;
    LatencyStats match_lat;
    SPSCQueue<Trade>& trade_q_;
    MPSCQueue<ExecutionReport>& report_q_;
private:
    MPSCQueue<Order>& order_q_;
    std::unordered_map<std::string, OrderBook> books_;
};

