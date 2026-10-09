#pragma once
#include <string>
#include "order.h"

enum class MessageType{
    NEW,
    CANCEL,
    MODIFY,
    PNL
};

struct Message{
    MessageType type = MessageType::NEW;
    uint64_t orderId = 0;
    Side side = Side::BUY;
    char symbol[8] = {0};
    int quantity = 0;
    int price = 0;
    OrderType orderType = OrderType::LIMIT;
};

class MessageConverter{
public:
    static Order toOrder(const Message &msg);
};

class Protocol{
    public:
        static Message parse(const std::string & data);
};