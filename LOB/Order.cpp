#include "Order.hpp"

#include <stdexcept>

Order::Order(
    OrderId id,
    OrderAction action,
    Quantity quantity,
    std::optional<Price> price
) : id_(id),
    action_(action),
    state_(OrderState::NEW),
    price_(price),
    originalQuantity_(quantity),
    remainingQuantity_(quantity)
{
}

Order::OrderId Order::id() const {
    return id_;
}
OrderAction Order::action() const {
    return action_;
}

OrderState Order::state() const {
    return state_;
}

Order::Quantity Order::originalQuantity() const {
    return originalQuantity_;
}

Order::Quantity Order::remainingQuantity() const {
    return remainingQuantity_;
}

const std::optional<Order::Price>& Order::price() const {
    return price_;
}

void Order::cancel() {
    if (state_ != OrderState::ACTIVE) {
        throw std::logic_error("Only an active order can be cancelled");
    }
    state_ = OrderState::CANCELLED;
}

void Order::activate() {
    if (state_ != OrderState::NEW) {
        throw std::logic_error("Only a new order can be activated");
    }
    state_ = OrderState::ACTIVE;
}

void Order::fill(Quantity quantity) {
    if (state_ != OrderState::ACTIVE) {
        throw std::logic_error("Only an active order can be filled");
    }
    if (quantity == 0) {
        throw std::invalid_argument("Fill quantity must be greater than zero");
    }
    if (quantity > remainingQuantity_) {
        throw std::invalid_argument("Fill quantity exceeds remaining quantity");
    }

    remainingQuantity_ -= quantity;
    if (remainingQuantity_ == 0) {
        state_ = OrderState::FILLED;
    }
}
