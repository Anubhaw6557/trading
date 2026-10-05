#include "client_session.h"
#include <iostream>
#include <thread>
#include "message.h"
#include <utility>

ClientSession::ClientSession(boost::asio::ip::tcp::socket socket,int trader_id,MPSCQueue<Order> &order_q,ReportDispatcher &dispatcher,MPSCQueue<Pnlrequest> &pnl_q)
    : socket_(std::move(socket)),
    order_q_(order_q),
    dispatcher_(dispatcher),
    trader_id_(trader_id),
    report_q_(1024),
    pnl_q_(pnl_q) {
}

void ClientSession::start() {
    std::cout<<"Session started with client: " << socket_.remote_endpoint() << "\n";
    std::thread writer(&ClientSession::writerLoop,this);
    readerLoop();
    running_ = false;
    writer.join();
    std::cout<<"Client disconnected: \n";
}

void ClientSession::readerLoop(){
    while(running_){
        char buffer[1024];
        boost::system::error_code erc;
        std::size_t bytes = socket_.read_some(boost::asio::buffer(buffer), erc);
        if(erc){
            running_ = false;
            break;
        }
        std::string data(buffer,bytes);
        Message msg = Protocol::parse(data);
        if(!registered_){
            dispatcher_.RegisterClient(trader_id_,&report_q_);
            registered_ = true;
        }
        if(msg.type == MessageType::PNL){
            Pnlrequest request;
            request.trader_id = trader_id_;
            request.symbol = msg.symbol;
            while(!pnl_q_.push(request)){
                ++pnl_q_.queue_spins_in;
                std::this_thread::yield();
            }
            continue;
        }
        Order order = MessageConverter::toOrder(msg);
        order.trader_id = trader_id_;
        while (!order_q_.push(order)) {
            ++order_q_.queue_spins_in;
        }
    }
}

void ClientSession::writerLoop(){
    while(running_){
        boost::system::error_code erc;
        ExecutionReport r;
        while(!report_q_.pop(r)){
            ++report_q_.queue_spins_out;
        }
        std::string response;
        switch (r.type){
            case ExecType::ACK :
                response = "ACK," + std::to_string(r.id) + "," +r.symbol+ "\n";
                break;
            case ExecType::FILL :
                response = "FILL," + std::to_string(r.id)+ "," +r.symbol +"," + std::to_string(r.fillqty)+"," + std::to_string(r.price) + "\n";
                break;
            case ExecType::PARTIAL_FILL :
                response = "PARTIAL_FILL," + std::to_string(r.id)+ "," +r.symbol +"," + std::to_string(r.fillqty)+"," + std::to_string(r.price) + "\n";
                break;
            case ExecType::CANCELLED :
                response = "CANCELLED," + std::to_string(r.id)+ "," +r.symbol  + "\n";
                break;
            case ExecType::REJECT :
                response = "REJECT," + std::to_string(r.id)+ "," +r.symbol  + "\n";
                break;
            case ExecType::MODIFIED :
                response = "MODIFIED," + std::to_string(r.id)+ "," +r.symbol  + "," + std::to_string(r.remqty) + "," + std::to_string(r.price) + "\n";
                break;
            case ExecType::PNL_UPDATE:
                response = "PNL_UPDATE," +r.symbol  + "," + std::to_string(r.pos) + ",$" + std::to_string(r.cash) + "\n";
                break;
            default :
                continue;
        }
        boost::asio::write(socket_, boost::asio::buffer(response), erc);
        if(erc){
            running_ = false;
            break;
        }
    }
}