#include <matching_engine.h>
#include <report.h>
#include <iostream>
#include <rdtsc.h>

void OrderBook::match(Order& o,uint64_t& trade_id,SPSCQueue<Trade>& trade_q_,MPSCQueue<ExecutionReport>& report_q_){
    if (o.side == Side::BUY) {
        auto& book = asks_;
        while (o.qty > 0 && !book.empty()) {
            auto it = book.begin();
            if (o.type == OrderType::LIMIT && it->first > o.price) break;
            auto& resting = it->second.front();
            int traded = std::min(o.qty, resting.qty);
            o.qty -= traded;
            resting.qty -= traded;
            int price = (o.type == OrderType::LIMIT) ? o.price : resting.price;
            Trade t{
                trade_id++,
                o.trader_id,resting.trader_id,
                o.symbol,price,
                traded,
                now_tsc(),0
            };
            while (!trade_q_.push(t)){
                ++trade_q_.queue_spins_in;
            }
            ExecutionReport r1;
            r1.trader_id = resting.trader_id;
            r1.fillqty = traded;
            r1.price = price;
            r1.id = resting.id;
            r1.remqty = resting.qty;
            r1.symbol = resting.symbol;
            if(r1.remqty == 0){
                r1.type = ExecType::FILL;
            }
            else {
                r1.type = ExecType::PARTIAL_FILL;
            }
            while (!report_q_.push(r1)){
                ++report_q_.queue_spins_in;
            }
            ExecutionReport r2;
            if(o.qty > 0) r2.type = ExecType::PARTIAL_FILL;
            else r2.type = ExecType::FILL;
            r2.trader_id = o.trader_id;
            r2.fillqty = traded;
            r2.price = price;
            r2.id = o.id;
            r2.remqty = o.qty - traded;
            r2.symbol = o.symbol;
            while (!report_q_.push(r2)){
                ++report_q_.queue_spins_in;
            }
            if (resting.qty == 0){
                it->second.pop_front();
                if (it->second.empty()) book.erase(it);
            }
        }
        if (o.qty > 0 && o.type == OrderType::LIMIT){
            bids_[o.price].push_back(o);
            auto it = std::prev(bids_[o.price].end());
            order_index_[o.id] = {
                Side::BUY,
                o.symbol,
                o.price,
                it
            };
        }
    }
    else {
        auto& book = bids_;
        while (o.qty > 0 && !book.empty()) {
            auto it = book.begin();
            if (o.type == OrderType::LIMIT && it->first < o.price) break;
            auto& resting = it->second.front();
            int traded = std::min(o.qty, resting.qty);
            o.qty -= traded;
            resting.qty -= traded;
            int price = (o.type == OrderType::LIMIT) ? o.price : resting.price;
            Trade t{
                trade_id++,
                resting.trader_id,o.trader_id,
                o.symbol,price,
                traded,
                now_tsc(),0
            };
            while (!trade_q_.push(t)){
                ++trade_q_.queue_spins_in;
            }
            ExecutionReport r1;
            r1.trader_id = resting.trader_id;
            r1.fillqty = traded;
            r1.price = price;
            r1.id = resting.id;
            r1.remqty = resting.qty;
            r1.symbol = resting.symbol;
            if(r1.remqty == 0){
                r1.type = ExecType::FILL;
            }
            else {
                r1.type = ExecType::PARTIAL_FILL;
            }
            while (!report_q_.push(r1)){
                ++report_q_.queue_spins_in;
            }
            ExecutionReport r2;
            if(o.qty > 0) r2.type = ExecType::PARTIAL_FILL;
            else r2.type = ExecType::FILL;
            r2.trader_id = o.trader_id;
            r2.fillqty = traded;
            r2.price = price;
            r2.id = o.id;
            r2.remqty = o.qty - traded;
            r2.symbol = o.symbol;
            while (!report_q_.push(r2)){
                ++report_q_.queue_spins_in;
            }
            if (resting.qty == 0){
                it->second.pop_front();
                if (it->second.empty()) book.erase(it);
            }
        }
        if (o.qty > 0 && o.type == OrderType::LIMIT){
            asks_[o.price].push_back(o);
            auto it = std::prev(asks_[o.price].end());
            order_index_[o.id] = {
                Side::SELL,
                o.symbol,
                o.price,
                it
            };
        }
    }
}

bool OrderBook::cancel(uint64_t id,MPSCQueue<ExecutionReport>& report_q_){
    auto idx = order_index_.find(id);
    if(idx == order_index_.end()) return false;
    auto &loc = idx->second;
    const Order cancelled = *loc.order_it;
    if(loc.side == Side::BUY){
        auto book_it = bids_.find(loc.price);
        book_it->second.erase(loc.order_it);
        if(book_it->second.empty()) bids_.erase(book_it);
        order_index_.erase(idx);
    }
    else{
        auto book_it = asks_.find(loc.price);
        book_it->second.erase(loc.order_it);
        if(book_it->second.empty()) asks_.erase(book_it);
        order_index_.erase(idx);
    }
    ExecutionReport r;
    r.type = ExecType::CANCELLED;
    r.id = id;
    r.symbol = cancelled.symbol;
    r.trader_id = cancelled.trader_id;
    r.remqty = cancelled.qty;
    while(!report_q_.push(r)){
        ++report_q_.queue_spins_in;
    }
    return true;
}

bool OrderBook::modify(Order& o,uint64_t& trade_id,SPSCQueue<Trade>& trade_q_,MPSCQueue<ExecutionReport>& report_q_){
    auto idx = order_index_.find(o.id);
    if(idx == order_index_.end()) return false;
    if(o.qty <= 0) return false;

    auto &loc = idx->second;
    Order& resting = *loc.order_it;
    const int old_price = resting.price;

    auto push_modified = [&](const Order& updated){
        ExecutionReport r;
        r.type = ExecType::MODIFIED;
        r.id = updated.id;
        r.symbol = updated.symbol;
        r.trader_id = updated.trader_id;
        r.price = updated.price;
        r.remqty = updated.qty;
        while(!report_q_.push(r)){
            ++report_q_.queue_spins_in;
        }
    };

    if(o.price == old_price){
        resting.qty = o.qty;
        push_modified(resting);
        return true;
    }

    Order updated = resting;
    updated.price = o.price;
    updated.qty = o.qty;

    if(loc.side == Side::BUY){
        auto book_it = bids_.find(loc.price);
        book_it->second.erase(loc.order_it);
        if(book_it->second.empty()) bids_.erase(book_it);
    }
    else{
        auto book_it = asks_.find(loc.price);
        book_it->second.erase(loc.order_it);
        if(book_it->second.empty()) asks_.erase(book_it);
    }
    order_index_.erase(idx);

    match(updated,trade_id,trade_q_,report_q_);
    push_modified(updated);
    return true;
}

void MatchingEngine::run(){
    Order o;
    uint64_t trade_id = 0;
    uint64_t process = 0;
    constexpr uint64_t Warmup = 0;
    while (true) {
        if (!order_q_.pop(o)){
            ++order_q_.queue_spins_out;
            continue;
        }
        if (o.type == OrderType::SHUTDOWN) break;
        o.t_emitted = now_tsc();
        ExecutionReport report;
        report.type = ExecType::ACK;
        report.id = o.id;
        report.symbol = o.symbol;
        report.trader_id = o.trader_id;
        while(!(report_q_.push(report))){
            ++report_q_.queue_spins_in;
        }
        if(o.rtype == RequestType::CANCEL){
            if(!books_[o.symbol].cancel(o.id,report_q_)){
                ExecutionReport reject;
                reject.type = ExecType::REJECT;
                reject.id = o.id;
                reject.symbol = o.symbol;
                reject.trader_id = o.trader_id;
                while(!report_q_.push(reject)){
                    ++report_q_.queue_spins_in;
                }
            }
            continue;
        }
        if(o.rtype == RequestType::MODIFY){
            if(!books_[o.symbol].modify(o,trade_id,trade_q_,report_q_)){
                ExecutionReport reject;
                reject.type = ExecType::REJECT;
                reject.id = o.id;
                reject.symbol = o.symbol;
                reject.trader_id = o.trader_id;
                while(!report_q_.push(reject)){
                    ++report_q_.queue_spins_in;
                }
            }
            continue;
        }
        books_[o.symbol].match(o,trade_id,trade_q_,report_q_);
        o.t_matched = now_tsc();
        if(++process > Warmup){
            match_lat.add(o.t_matched-o.t_emitted);
            ingress_lat.add(o.t_emitted-o.t_created);
        }
    }
    std::cout<<"No. of trades == "<<trade_id<<"\n";
    Trade shutdown{};
    shutdown.trade_buyer_id = -1;
    while (!trade_q_.push(shutdown)){
        ++trade_q_.queue_spins_in;
    }
    ExecutionReport shutdown_r{};
    shutdown_r.trader_id = -1;
    while (!report_q_.push(shutdown_r)){
        ++report_q_.queue_spins_in;
    }
}