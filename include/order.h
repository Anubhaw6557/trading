#pragma once

#include <cstdint>
#include <string>
#include <cstring>

enum class Side { BUY, SELL };
enum class OrderType { LIMIT , MARKET , SHUTDOWN};
enum class RequestType {NEW , CANCEL , MODIFY , PNL};

inline void set_symbol(char dest[8], const std::string& src) {
    std::memset(dest, 0, 8);
    std::strncpy(dest, src.c_str(), 7);
}

inline std::string get_symbol(const char src[8]) {
    return std::string(src, strnlen(src, 8));
}

struct Order {
    uint64_t id = 0;
    int trader_id = 0;
    RequestType rtype = RequestType::NEW;
    char symbol[8] = {0};
    Side side = Side::BUY;
    OrderType type = OrderType::LIMIT;
    int price = 0;
    int qty = 0;
    uint64_t t_created = 0;
    uint64_t t_emitted = 0;
    uint64_t t_matched = 0;
};

struct OrderNode {
    uint64_t id = 0;
    int trader_id = 0;
    Side side = Side::BUY;
    OrderType type = OrderType::LIMIT;
    int price = 0;
    int qty = 0;
    uint64_t t_created = 0;
    uint64_t t_emitted = 0;
    uint64_t t_matched = 0;
    char symbol[8] = {0};

    OrderNode* prev = nullptr;
    OrderNode* next = nullptr;
};

struct Pnlrequest {
    int trader_id = 0;
    char symbol[8] = {0};
};

struct Trade {
    uint64_t trade_id = 0;
    int trade_buyer_id = 0;
    int trade_seller_id = 0;
    char symbol[8] = {0};
    int price = 0;
    int qty = 0;
    uint64_t t_created = 0;
    uint64_t t_emitted = 0;
};

struct OrderLocation {
    Side side;
    char symbol[8] = {0};
    int price = 0;
    OrderNode* node = nullptr;
};


