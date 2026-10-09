#pragma once
#include "order.h"
#include "report.h"
#include "spsc_queue.h"
#include "mpsc_queue.h"
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cassert>

// ============================================================================
// ObjectPool: Pre-allocated contiguous memory pool for OrderNode.
// Zero heap allocation during trading. O(1) alloc/free via free-list.
// ============================================================================
template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t capacity) : capacity_(capacity) {
        pool_.resize(capacity);
        free_list_.reserve(capacity);
        for (size_t i = capacity; i > 0; --i) {
            free_list_.push_back(&pool_[i - 1]);
        }
    }

    T* alloc() {
        if (free_list_.empty()) return nullptr;
        T* ptr = free_list_.back();
        free_list_.pop_back();
        *ptr = T{};
        return ptr;
    }

    void free(T* ptr) {
        free_list_.push_back(ptr);
    }

    size_t available() const { return free_list_.size(); }
    size_t capacity() const { return capacity_; }

private:
    size_t capacity_;
    std::vector<T> pool_;
    std::vector<T*> free_list_;
};

// ============================================================================
// PriceLevel: Intrusive doubly-linked FIFO queue of OrderNodes at a price.
// O(1) push_back, O(1) remove (any position), O(1) pop_front.
// ============================================================================
struct PriceLevel {
    int price = 0;
    int count = 0;
    int total_qty = 0;
    OrderNode* head = nullptr;
    OrderNode* tail = nullptr;

    bool empty() const { return head == nullptr; }

    void push_back(OrderNode* node) {
        node->next = nullptr;
        node->prev = tail;
        if (tail) tail->next = node;
        else head = node;
        tail = node;
        ++count;
        total_qty += node->qty;
    }

    void remove(OrderNode* node) {
        if (node->prev) node->prev->next = node->next;
        else head = node->next;
        if (node->next) node->next->prev = node->prev;
        else tail = node->prev;
        --count;
        total_qty -= node->qty;
        node->prev = node->next = nullptr;
    }

    OrderNode* pop_front() {
        if (!head) return nullptr;
        OrderNode* node = head;
        head = node->next;
        if (head) head->prev = nullptr;
        else tail = nullptr;
        --count;
        total_qty -= node->qty;
        node->prev = node->next = nullptr;
        return node;
    }
};

constexpr int MAX_PRICE = 500000;

// ============================================================================
// OrderBook: Price-time priority order book with O(1) operations.
// ============================================================================
class OrderBook {
public:
    explicit OrderBook(ObjectPool<OrderNode>* pool = nullptr)
        : bids_(MAX_PRICE), asks_(MAX_PRICE), pool_(pool) {}

    void set_pool(ObjectPool<OrderNode>* pool) { pool_ = pool; }

    void match(Order& o, uint64_t& trade_id,
               SPSCQueue<Trade>& trade_q, MPSCQueue<ExecutionReport>& report_q);
    bool cancel(uint64_t id, MPSCQueue<ExecutionReport>& report_q);
    bool modify(Order& o, uint64_t& trade_id,
                SPSCQueue<Trade>& trade_q, MPSCQueue<ExecutionReport>& report_q);

private:
    std::vector<PriceLevel> bids_;
    std::vector<PriceLevel> asks_;
    int best_bid_ = -1;
    int best_ask_ = MAX_PRICE;

    std::unordered_map<uint64_t, OrderNode*> order_index_;

    ObjectPool<OrderNode>* pool_ = nullptr;

    OrderNode* alloc_node(const Order& o);
    void free_node(OrderNode* node);

    void try_match_buy(Order& o, uint64_t& trade_id,
                       SPSCQueue<Trade>& trade_q, MPSCQueue<ExecutionReport>& report_q);
    void try_match_sell(Order& o, uint64_t& trade_id,
                        SPSCQueue<Trade>& trade_q, MPSCQueue<ExecutionReport>& report_q);
};
