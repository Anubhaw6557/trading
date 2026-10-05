#include "matching_engine.h"
#include "order.h"
#include "tcp_server.h"
#include "report.h"
#include "client_session.h"
#include "metrics.h"
#include "mpsc_queue.h"
#include "message.h"
#include <thread>
#include <iostream>
#include <iomanip>

void market_replay(MPSCQueue<Order>&);
void pnl_thread(SPSCQueue<Trade>&,MPSCQueue<Pnlrequest>&,MPSCQueue<ExecutionReport>&, LatencyStats&);

static inline uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int main() {
    constexpr size_t Order_queue_size = 1<<11;
    constexpr size_t Trade_queue_size = 1<<11;
    constexpr size_t Report_queue_size = 1<<11;
    constexpr size_t Pnl_queue_size = 1<<11;
    MPSCQueue<Order> order_q(Order_queue_size);
    MPSCQueue<Pnlrequest> pnl_q(Pnl_queue_size);
    SPSCQueue<Trade> trade_q(Trade_queue_size);
    MPSCQueue<ExecutionReport> report_q(Report_queue_size);
    PerformanceMetrics metrics_;
    ReportDispatcher dispatcher(report_q,metrics_);
    MatchingEngine engine(order_q, trade_q,report_q);
    LatencyStats metrics_lat;
    uint64_t t1 = now_ns();
    std::thread match(&MatchingEngine::run, &engine);
    std::thread pnl(pnl_thread, std::ref(trade_q),std::ref(pnl_q),std::ref(report_q),std::ref(metrics_lat));
    std::thread dispatcherthread(&ReportDispatcher::run,&dispatcher);
    TCPServer server(8080,order_q,dispatcher,pnl_q);
    std::thread serverthread(&TCPServer::start,&server);

    std::cout<<"Press ENTER TO SHUTDOWN ....\n";
    std::cin.get();
    server.stop();
    serverthread.join();
    Order shutdown{};
    shutdown.type = OrderType::SHUTDOWN;
    while(!order_q.push(shutdown)){
        std::this_thread::yield();
    }
    match.join();
    pnl.join();
    dispatcherthread.join();

    std::cout<<"\n----Latency-----\n";
    engine.ingress_lat.report("Order Ingress");
    engine.match_lat.report("Order Matched");
    metrics_lat.report("Trades Ingress");
    uint64_t t2 = now_ns();
    std::cout<<"\n----Metrics-----\n";
    metrics_.report(t2-t1);
}