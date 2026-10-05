#pragma once

#include <cstdint>
#include <string>

struct MetricsSnapshot {
    uint64_t acks = 0;
    uint64_t fills = 0;
    uint64_t partial_fills = 0;
    uint64_t cancelled = 0;
    uint64_t rejects = 0;
    uint64_t modified = 0;
    uint64_t pnl_updates = 0;
    uint64_t round_turns = 0;
    uint64_t report_q_spins_in = 0;
    uint64_t report_q_spins_out = 0;
    uint64_t client_push_spins = 0;
};

enum class ExecType{
    ACK,
    FILL,
    PARTIAL_FILL,
    CANCELLED,
    REJECT,
    MODIFIED,
    PNL_UPDATE
};

struct ExecutionReport{
    ExecType type;
    uint64_t id = 0;
    std::string symbol;
    int fillqty = 0;
    int remqty = 0;
    int price = 0;
    int trader_id = 0;
    int64_t cash = 0;
    int64_t pos = 0;
    MetricsSnapshot stats{};
};
