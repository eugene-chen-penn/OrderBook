#include "PriceLevel.hpp"

#include <algorithm>
#include <stdexcept>

PriceLevel::PriceLevel(Order::Price price)
    : price_(price)
{
}

PriceLevel::PriceLevel(Order::Price price, Order& order)
    : PriceLevel(price)
{
    addOrder(order);
}

Order::Quantity PriceLevel::totalQuantity() const noexcept {
    return totalQuantity_;
}

Order::Price PriceLevel::price() const noexcept {
    return price_;
}

void PriceLevel::addOrder(Order& order) {
    orders_.push_back(order);
    totalQuantity_ += order.remainingQuantity();
}

bool PriceLevel::removeOrder(Order::OrderId orderId) {
    for (const std::reference_wrapper<Order> orderReference : orders_) {
        Order& order = orderReference.get();
        if (order.id() == orderId && order.state() == OrderState::ACTIVE) {
            totalQuantity_ -= order.remainingQuantity();
            order.cancel();
            return true;
        }
    }
    return false;
}

void PriceLevel::discardInactiveOrders() noexcept {
    while (!orders_.empty() && orders_.front().get().state() != OrderState::ACTIVE) {
        orders_.pop_front();
    }
}

std::optional<std::reference_wrapper<Order>> PriceLevel::topOrder() noexcept {
    discardInactiveOrders();
    if (orders_.empty()) {
        return std::nullopt;
    }
    return orders_.front();
}

std::optional<std::reference_wrapper<Order>> PriceLevel::topOrder() const noexcept {
    for (const std::reference_wrapper<Order> orderReference : orders_) {
        Order& order = orderReference.get();
        if (order.state() == OrderState::ACTIVE) {
            return order;
        }
    }
    return std::nullopt;
}

void PriceLevel::fillQuantity(Order::Quantity quantity) {
    if (quantity > totalQuantity_) {
        throw std::invalid_argument("Fill quantity exceeds price-level quantity");
    }

    while (quantity > 0) {
        if (orders_.empty()) {
            throw std::runtime_error("No orders at this price to fill");
        }
        const Order::Quantity fillQuantity =
            std::min(quantity, orders_.front().get().remainingQuantity());
        fillTopOrder(fillQuantity);
        quantity -= fillQuantity;
    }
}

void PriceLevel::fillTopOrder(Order::Quantity quantity) {
    discardInactiveOrders();
    if (orders_.empty()) {
        throw std::runtime_error("No orders at this price to fill");
    }

    Order& order = orders_.front().get();
    if (quantity == 0 || quantity > order.remainingQuantity()) {
        throw std::invalid_argument("Invalid fill quantity for the top order");
    }

    order.fill(quantity);
    totalQuantity_ -= quantity;
    if (order.remainingQuantity() == 0) {
        orders_.pop_front();
    }
}
