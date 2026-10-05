#include "tcp_server.h"
#include "client_session.h"
#include <iostream>
#include "report_dispatcher.h"
#include <memory>
#include <thread>
using boost::asio::ip::tcp;

TCPServer::TCPServer(unsigned short port,MPSCQueue<Order> &order_q,ReportDispatcher &dispatcher,MPSCQueue<Pnlrequest> &pnl_q)
    :acceptor(ioContext,tcp::endpoint(tcp::v4(),port)),
    order_q_(order_q),
    dispatcher_(dispatcher),
    pnl_q_(pnl_q){
}

void TCPServer::start(){
    std::cout<<"Trading Server Strated\n";
    std::cout<<"Listening on port 8080..\n";
    boost::system::error_code ec;
    acceptor.non_blocking(true,ec);
    while(running_){
        tcp::socket socket(ioContext);
        acceptor.accept(socket,ec);
        if(!running_) break;
        if(ec == boost::asio::error::would_block || ec == boost::asio::error::try_again){
            std::this_thread::yield();
            continue;
        }
        std::cout<<"Client Connected : " << socket.remote_endpoint() << "\n";
        int trader_id = next_trader_id_++;
        auto session = std::make_shared<ClientSession>(std::move(socket),trader_id,order_q_,dispatcher_,pnl_q_);
        std::thread(&ClientSession::start,session).detach();
    }
}

void TCPServer::stop(){
    running_ = false;
    boost::system::error_code erc;
    acceptor.close(erc);
}

ReportDispatcher::ReportDispatcher(MPSCQueue<ExecutionReport> &report_q,PerformanceMetrics &metrics)
    : report_q_(report_q),
      metrics_(metrics) {}

void ReportDispatcher::dispatch(ExecutionReport& r) {
    metrics_.record(r.type);
    r.stats = metrics_.snapshot(report_q_.queue_spins_in, report_q_.queue_spins_out);
    SPSCQueue<ExecutionReport>* queue = nullptr;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        auto it = clients_.find(r.trader_id);
        if(it != clients_.end()) queue = it->second;
    }
    if(!queue) return;
    while(!queue->push(r)){
        ++metrics_.client_push_spins;
        std::this_thread::yield();
    }
}

void ReportDispatcher::run(){
    while (true){
        ExecutionReport r;
        while(!report_q_.pop(r)){
            ++report_q_.queue_spins_out;
            std::this_thread::yield();
        }
        if (r.trader_id == -1){
            break;
        }
        dispatch(r);
    }
}

void ReportDispatcher::RegisterClient(int trader_id,SPSCQueue<ExecutionReport> *queue){
    std::lock_guard<std::mutex> lock(client_mutex_);
    clients_[trader_id] = queue;
}

void ReportDispatcher::unregisterClient(int trader_id){
    std::lock_guard<std::mutex> lock(client_mutex_);
    clients_.erase(trader_id);
}
