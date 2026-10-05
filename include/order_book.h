#pragma once
#include "order.h"
#include <report.h>
#include <map>
#include <unordered_map>
#include <deque>
#include <vector>
#include "spsc_queue.h"
#include "mpsc_queue.h"

class OrderBook {
public:
    void match(Order& o,uint64_t& trade_id,SPSCQueue<Trade>& trade_q_,MPSCQueue<ExecutionReport>& report_q_);
    bool cancel(uint64_t id,MPSCQueue<ExecutionReport>& report_q_);
    bool modify(Order& o,uint64_t& trade_id,SPSCQueue<Trade>& trade_q_,MPSCQueue<ExecutionReport>& report_q_);
private:
    std::map<int, std::deque<Order>, std::greater<int>> bids_;
    std::map<int, std::deque<Order>> asks_;
    std::unordered_map<uint64_t,OrderLocation> order_index_;
};
