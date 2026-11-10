#pragma once

#include "Order.hpp"

#include <cstddef>
#include <vector>

struct Trade {
    Order::OrderId buyOrderId;
    Order::OrderId sellOrderId;
    Order::Quantity quantity;
    Order::Price price;
};

class Trades {
public:
    void log(
        const Order& buyOrder,
        const Order& sellOrder,
        Order::Quantity quantity,
        Order::Price price
    );

    const std::vector<Trade>& records() const noexcept;
    std::size_t size() const noexcept;

private:
    std::vector<Trade> records_;
};
