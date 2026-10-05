#pragma once

#include <cstdint>
#include <string>
#include <deque>
enum class Side { BUY, SELL };
enum class OrderType { LIMIT , MARKET , SHUTDOWN};
enum class RequestType {NEW , CANCEL , MODIFY , PNL};

struct Order {
    uint64_t id = 0;
    int trader_id = 0;
    RequestType rtype = RequestType::NEW;
    std::string symbol;
    Side side = Side::BUY;
    OrderType type = OrderType::LIMIT;
    int price = 0;
    int qty = 0;
    uint64_t t_created = 0;
    uint64_t t_emitted = 0;
    uint64_t t_matched = 0;
};

struct Pnlrequest{
    int trader_id;
    std::string symbol;
};

struct Trade {
    uint64_t trade_id;
    int trade_buyer_id;
    int trade_seller_id;
    std::string symbol;
    int price;
    int qty;
    uint64_t t_created;
    uint64_t t_emitted;
};

struct OrderLocation{
    Side side;
    std::string symbol;
    int price;
    std::deque<Order>::iterator order_it;
};

