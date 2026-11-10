#pragma once

#include "Order.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>

class PriceLevel {
public:
    explicit PriceLevel(Order::Price price);
    PriceLevel(Order::Price price, Order& order);

    Order::Quantity totalQuantity() const noexcept;
    Order::Price price() const noexcept;

    std::optional<std::reference_wrapper<Order>> topOrder() noexcept;
    std::optional<std::reference_wrapper<Order>> topOrder() const noexcept;

    void addOrder(Order& order);
    bool removeOrder(Order::OrderId orderId);
    void fillTopOrder(Order::Quantity quantity);
    void fillQuantity(Order::Quantity quantity);

private:
    void discardInactiveOrders() noexcept;

    Order::Price price_;
    Order::Quantity totalQuantity_{0};
    std::deque<std::reference_wrapper<Order>> orders_;
};
