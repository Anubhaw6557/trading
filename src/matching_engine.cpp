#include <matching_engine.h>
#include <report.h>
#include <iostream>
#include <rdtsc.h>
#include <cstring>

// ============================================================================
// OrderBook: alloc/free helpers
// ============================================================================

OrderNode* OrderBook::alloc_node(const Order& o) {
    OrderNode* node = pool_->alloc();
    if (!node) return nullptr;
    node->id = o.id;
    node->trader_id = o.trader_id;
    node->side = o.side;
    node->type = o.type;
    node->price = o.price;
    node->qty = o.qty;
    node->t_created = o.t_created;
    std::memcpy(node->symbol, o.symbol, 8);
    node->prev = nullptr;
    node->next = nullptr;
    return node;
}

void OrderBook::free_node(OrderNode* node) {
    pool_->free(node);
}

// ============================================================================
// OrderBook::match — Price-time priority matching
// ============================================================================

void OrderBook::match(Order& o, uint64_t& trade_id,
                      SPSCQueue<Trade>& trade_q_, MPSCQueue<ExecutionReport>& report_q_) {
    if (o.side == Side::BUY) {
        try_match_buy(o, trade_id, trade_q_, report_q_);
    } else {
        try_match_sell(o, trade_id, trade_q_, report_q_);
    }
}

void OrderBook::try_match_buy(Order& o, uint64_t& trade_id,
                               SPSCQueue<Trade>& trade_q_, MPSCQueue<ExecutionReport>& report_q_) {
    // Match against asks from the lowest active price level.
    while (o.qty > 0 && best_ask_ < MAX_PRICE) {
        if (o.type == OrderType::LIMIT && best_ask_ > o.price) break;

        PriceLevel& level = asks_[best_ask_];
        while (o.qty > 0 && !level.empty()) {
            OrderNode* resting = level.head;
            int traded = std::min(o.qty, resting->qty);
            o.qty -= traded;
            resting->qty -= traded;
            level.total_qty -= traded;
            int price = (o.type == OrderType::LIMIT) ? o.price : resting->price;

            Trade t{};
            t.trade_id = trade_id++;
            t.trade_buyer_id = o.trader_id;
            t.trade_seller_id = resting->trader_id;
            std::memcpy(t.symbol, o.symbol, 8);
            t.price = price;
            t.qty = traded;
            t.t_created = now_tsc();
            while (!trade_q_.push(t)) {
                ++trade_q_.queue_spins_in;
            }

            // Execution report for resting order
            ExecutionReport r1{};
            r1.trader_id = resting->trader_id;
            r1.fillqty = traded;
            r1.price = price;
            r1.id = resting->id;
            r1.remqty = resting->qty;
            std::memcpy(r1.symbol, resting->symbol, 8);
            r1.type = (resting->qty == 0) ? ExecType::FILL : ExecType::PARTIAL_FILL;
            while (!report_q_.push(r1)) {
                ++report_q_.queue_spins_in;
            }

            // Execution report for aggressor
            ExecutionReport r2{};
            r2.type = (o.qty > 0) ? ExecType::PARTIAL_FILL : ExecType::FILL;
            r2.trader_id = o.trader_id;
            r2.fillqty = traded;
            r2.price = price;
            r2.id = o.id;
            r2.remqty = o.qty;
            std::memcpy(r2.symbol, o.symbol, 8);
            while (!report_q_.push(r2)) {
                ++report_q_.queue_spins_in;
            }

            if (resting->qty == 0) {
                order_index_.erase(resting->id);
                level.pop_front();
                free_node(resting);
            }
        }
        if (level.empty()) {
            do {
                ++best_ask_;
            } while (best_ask_ < MAX_PRICE && asks_[best_ask_].empty());
        }
    }

    // Place remainder on book
    if (o.qty > 0 && o.type == OrderType::LIMIT) {
        OrderNode* node = alloc_node(o);
        if (node) {
            bids_[o.price].push_back(node);
            bids_[o.price].price = o.price;
            if (o.price > best_bid_) best_bid_ = o.price;
            order_index_[o.id] = node;
        }
    }
}

void OrderBook::try_match_sell(Order& o, uint64_t& trade_id,
                                SPSCQueue<Trade>& trade_q_, MPSCQueue<ExecutionReport>& report_q_) {
    // Match against bids from the highest active price level.
    while (o.qty > 0 && best_bid_ >= 0) {
        if (o.type == OrderType::LIMIT && best_bid_ < o.price) break;

        PriceLevel& level = bids_[best_bid_];
        while (o.qty > 0 && !level.empty()) {
            OrderNode* resting = level.head;
            int traded = std::min(o.qty, resting->qty);
            o.qty -= traded;
            resting->qty -= traded;
            level.total_qty -= traded;
            int price = (o.type == OrderType::LIMIT) ? o.price : resting->price;

            Trade t{};
            t.trade_id = trade_id++;
            t.trade_buyer_id = resting->trader_id;
            t.trade_seller_id = o.trader_id;
            std::memcpy(t.symbol, o.symbol, 8);
            t.price = price;
            t.qty = traded;
            t.t_created = now_tsc();
            while (!trade_q_.push(t)) {
                ++trade_q_.queue_spins_in;
            }

            // Execution report for resting order
            ExecutionReport r1{};
            r1.trader_id = resting->trader_id;
            r1.fillqty = traded;
            r1.price = price;
            r1.id = resting->id;
            r1.remqty = resting->qty;
            std::memcpy(r1.symbol, resting->symbol, 8);
            r1.type = (resting->qty == 0) ? ExecType::FILL : ExecType::PARTIAL_FILL;
            while (!report_q_.push(r1)) {
                ++report_q_.queue_spins_in;
            }

            // Execution report for aggressor
            ExecutionReport r2{};
            r2.type = (o.qty > 0) ? ExecType::PARTIAL_FILL : ExecType::FILL;
            r2.trader_id = o.trader_id;
            r2.fillqty = traded;
            r2.price = price;
            r2.id = o.id;
            r2.remqty = o.qty;
            std::memcpy(r2.symbol, o.symbol, 8);
            while (!report_q_.push(r2)) {
                ++report_q_.queue_spins_in;
            }

            if (resting->qty == 0) {
                order_index_.erase(resting->id);
                level.pop_front();
                free_node(resting);
            }
        }
        if (level.empty()) {
            do {
                --best_bid_;
            } while (best_bid_ >= 0 && bids_[best_bid_].empty());
        }
    }

    // Place remainder on book
    if (o.qty > 0 && o.type == OrderType::LIMIT) {
        OrderNode* node = alloc_node(o);
        if (node) {
            asks_[o.price].push_back(node);
            asks_[o.price].price = o.price;
            if (o.price < best_ask_) best_ask_ = o.price;
            order_index_[o.id] = node;
        }
    }
}

// ============================================================================
// OrderBook::cancel — O(1) cancel via pointer unlinking
// ============================================================================

bool OrderBook::cancel(uint64_t id, MPSCQueue<ExecutionReport>& report_q_) {
    auto idx = order_index_.find(id);
    if (idx == order_index_.end()) return false;

    OrderNode* node = idx->second;

    if (node->side == Side::BUY) {
        PriceLevel& level = bids_[node->price];
        level.remove(node);
        if (level.empty() && node->price == best_bid_) {
            do {
                --best_bid_;
            } while (best_bid_ >= 0 && bids_[best_bid_].empty());
        }
    } else {
        PriceLevel& level = asks_[node->price];
        level.remove(node);
        if (level.empty() && node->price == best_ask_) {
            do {
                ++best_ask_;
            } while (best_ask_ < MAX_PRICE && asks_[best_ask_].empty());
        }
    }

    ExecutionReport r{};
    r.type = ExecType::CANCELLED;
    r.id = id;
    std::memcpy(r.symbol, node->symbol, 8);
    r.trader_id = node->trader_id;
    r.remqty = node->qty;
    while (!report_q_.push(r)) {
        ++report_q_.queue_spins_in;
    }

    order_index_.erase(idx);
    free_node(node);
    return true;
}

// ============================================================================
// OrderBook::modify — O(1) in-place qty update or O(1) re-insert on price change
// ============================================================================

bool OrderBook::modify(Order& o, uint64_t& trade_id,
                       SPSCQueue<Trade>& trade_q_, MPSCQueue<ExecutionReport>& report_q_) {
    auto idx = order_index_.find(o.id);
    if (idx == order_index_.end()) return false;
    if (o.qty <= 0) return false;

    OrderNode* node = idx->second;
    const int old_price = node->price;

    auto push_modified = [&](OrderNode* n) {
        ExecutionReport r{};
        r.type = ExecType::MODIFIED;
        r.id = n->id;
        std::memcpy(r.symbol, n->symbol, 8);
        r.trader_id = n->trader_id;
        r.price = n->price;
        r.remqty = n->qty;
        while (!report_q_.push(r)) {
            ++report_q_.queue_spins_in;
        }
    };

    // Same price: in-place qty update, no re-insertion needed
    if (o.price == old_price) {
        PriceLevel& level = (node->side == Side::BUY) ? bids_[old_price] : asks_[old_price];
        level.total_qty += o.qty - node->qty;
        node->qty = o.qty;
        push_modified(node);
        return true;
    }

    // Price changed: remove from old level, re-match at new price
    if (node->side == Side::BUY) {
        PriceLevel& level = bids_[node->price];
        level.remove(node);
        if (level.empty() && node->price == best_bid_) {
            do {
                --best_bid_;
            } while (best_bid_ >= 0 && bids_[best_bid_].empty());
        }
    } else {
        PriceLevel& level = asks_[node->price];
        level.remove(node);
        if (level.empty() && node->price == best_ask_) {
            do {
                ++best_ask_;
            } while (best_ask_ < MAX_PRICE && asks_[best_ask_].empty());
        }
    }
    order_index_.erase(idx);
    free_node(node);

    // Build updated order and re-match
    Order updated = o;
    updated.qty = o.qty;
    updated.price = o.price;
    match(updated, trade_id, trade_q_, report_q_);
    auto modified_idx = order_index_.find(o.id);
    if (modified_idx != order_index_.end()) {
        push_modified(modified_idx->second);
    }
    return true;
}

// ============================================================================
// MatchingEngine::run — Main event loop
// ============================================================================

void MatchingEngine::run() {
    Order o;
    uint64_t trade_id = 0;
    uint64_t process = 0;
    constexpr uint64_t Warmup = 0;

    while (true) {
        if (!order_q_.pop(o)) {
            ++order_q_.queue_spins_out;
            continue;
        }
        if (o.type == OrderType::SHUTDOWN) break;
        o.t_emitted = now_tsc();

        ExecutionReport report{};
        report.type = ExecType::ACK;
        report.id = o.id;
        std::memcpy(report.symbol, o.symbol, 8);
        report.trader_id = o.trader_id;
        while (!(report_q_.push(report))) {
            ++report_q_.queue_spins_in;
        }

        OrderBook& book = get_book(o.symbol);

        if (o.rtype == RequestType::CANCEL) {
            if (!book.cancel(o.id, report_q_)) {
                ExecutionReport reject{};
                reject.type = ExecType::REJECT;
                reject.id = o.id;
                std::memcpy(reject.symbol, o.symbol, 8);
                reject.trader_id = o.trader_id;
                while (!report_q_.push(reject)) {
                    ++report_q_.queue_spins_in;
                }
            }
            continue;
        }
        if (o.rtype == RequestType::MODIFY) {
            if (!book.modify(o, trade_id, trade_q_, report_q_)) {
                ExecutionReport reject{};
                reject.type = ExecType::REJECT;
                reject.id = o.id;
                std::memcpy(reject.symbol, o.symbol, 8);
                reject.trader_id = o.trader_id;
                while (!report_q_.push(reject)) {
                    ++report_q_.queue_spins_in;
                }
            }
            continue;
        }
        book.match(o, trade_id, trade_q_, report_q_);
        o.t_matched = now_tsc();
        if (++process > Warmup) {
            match_lat.add(o.t_matched - o.t_emitted);
            ingress_lat.add(o.t_emitted - o.t_created);
        }
    }
    std::cout << "No. of trades == " << trade_id << "\n";
    Trade shutdown{};
    shutdown.trade_buyer_id = -1;
    while (!trade_q_.push(shutdown)) {
        ++trade_q_.queue_spins_in;
    }
    ExecutionReport shutdown_r{};
    shutdown_r.trader_id = -1;
    while (!report_q_.push(shutdown_r)) {
        ++report_q_.queue_spins_in;
    }
}