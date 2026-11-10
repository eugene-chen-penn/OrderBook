#pragma once

#include <memory>

class Order;
class Trades;

class testStrategy {
public:
    bool signal(const Trades& trades) const;
    void execute(std::unique_ptr<Order> order) const;
private:

};