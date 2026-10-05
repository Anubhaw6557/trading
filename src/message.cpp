#include "message.h"
#include "rdtsc.h"

Order MessageConverter::toOrder(const Message &msg){
    Order order;
    if(msg.type == MessageType::CANCEL){
        order.rtype = RequestType::CANCEL;
        order.id = msg.orderId;
        order.symbol = msg.symbol;
        order.type = OrderType::LIMIT;
        return order;
    }
    if(msg.type == MessageType::MODIFY){
        order.rtype = RequestType::MODIFY;
        order.id = msg.orderId;
        order.symbol = msg.symbol;
        order.price = msg.price;
        order.qty = msg.quantity;
        order.type = OrderType::LIMIT;
        return order;
    }
    order.rtype = RequestType::NEW;
    order.id = msg.orderId;
    order.symbol = msg.symbol;
    order.trader_id = 1;
    order.side = msg.side;
    order.type = msg.orderType;
    order.price = msg.price;
    order.qty = msg.quantity;
    order.t_created = now_tsc();
    order.t_emitted = 0;
    order.t_matched = 0;
    return order;

}
