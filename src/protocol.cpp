#include "message.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <algorithm>

static std::string trim(std::string s){
    while(!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')){
        s.pop_back();
    }
    size_t start = 0;
    while(start < s.size() && (s[start] == ' ' || s[start] == '\t')){
        ++start;
    }
    return s.substr(start);
}

Message Protocol::parse(const std::string &data){
    Message msg;
    std::stringstream ss(data);
    std::string token;
    std::vector<std::string> fields;
    while(std::getline(ss,token,',')){
        fields.push_back(trim(token));
    }
    if(fields.size() == 2){
        if(fields[0] == "PNL"){
            msg.type = MessageType::PNL;
            msg.symbol = fields[1];
            return msg;
        }
    }
    if(fields.size() == 3){
        if(fields[0] == "CANCEL"){
            msg.type = MessageType::CANCEL;
            msg.orderId = std::stoull(fields[1]);
            msg.symbol = fields[2];
            return msg;
        } 
    }
    if(fields.size() == 5){
        if(fields[0] == "MODIFY"){
            msg.type = MessageType::MODIFY;
            msg.orderId = std::stoull(fields[1]);
            msg.symbol = fields[2];
            msg.quantity = std::stoi(fields[3]);
            msg.price = std::stoi(fields[4]);
            return msg;
        }
    }
    if(fields.size() != 6) return msg;
    if(fields[0] == "NEW") msg.type = MessageType::NEW;
    msg.orderId = std::stoull(fields[1]);
    if(fields[2] == "BUY") msg.side = Side::BUY;
    else msg.side = Side::SELL;
    msg.symbol = fields[3];
    msg.quantity =std::stoi(fields[4]);
    msg.price = std::stoi(fields[5]);
    return msg;
}