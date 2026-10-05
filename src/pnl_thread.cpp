#include "spsc_queue.h"
#include "mpsc_queue.h"
#include "order.h"
#include "latency.h"
#include "report.h"
#include "rdtsc.h"
#include <unordered_map>
#include <iostream>

struct PnL {
    int64_t cash = 0;
    int64_t pos = 0;
};

void pnl_thread(SPSCQueue<Trade>& tq,MPSCQueue<Pnlrequest> &pnl_q,MPSCQueue<ExecutionReport> &report_q,LatencyStats& metrics_lat) {
    std::unordered_map<int,std::unordered_map<std::string, PnL>> pnl;
    Trade t;
    Pnlrequest req;
    uint64_t process = 0;
    constexpr uint64_t Warmup = 0;
    while (true) {
        bool work = false;
        if(tq.pop(t)){
            work = true;
            if (t.trade_buyer_id == -1 || t.trade_seller_id == -1){
                break;
            }
            auto& buyer = pnl[t.trade_buyer_id][t.symbol];
            auto& seller = pnl[t.trade_seller_id][t.symbol];
            buyer.cash -= int64_t(t.price) * t.qty;
            buyer.pos += t.qty;
            seller.cash += int64_t(t.price) * t.qty;
            seller.pos -= t.qty;
            t.t_emitted = now_tsc();
            if(++process > Warmup){
                metrics_lat.add(t.t_emitted - t.t_created);
            }
        }
        if(pnl_q.pop(req)){
            work = true;
            auto& data = pnl[req.trader_id][req.symbol];
            ExecutionReport rep;
            rep.type = ExecType::PNL_UPDATE;
            rep.trader_id = req.trader_id;
            rep.symbol = req.symbol;
            rep.cash = data.cash;
            rep.pos = data.pos;
            while(!report_q.push(rep)){
                ++report_q.queue_spins_in;
                std::this_thread::yield();
            }
        }
        if(!work) std::this_thread::yield();
    }

    std::cout << "\n--- PnL ---\n";
    for (auto& [tr, mp] : pnl){
        for (auto& [sym, p] : mp){
            std::cout << "Trader " << tr
                      << " Sym " << sym
                      << " Cash " << p.cash
                      << " Pos " << p.pos << "\n";
        }
    }
}
