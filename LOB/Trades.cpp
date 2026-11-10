#include "Trades.hpp"

#include <stdexcept>

void Trades::log(
    const Order& buyOrder,
    const Order& sellOrder,
    Order::Quantity quantity,
    Order::Price price
) {
    if (buyOrder.action() != OrderAction::BUY ||
        sellOrder.action() != OrderAction::SELL) {
        throw std::invalid_argument("A trade requires one buy and one sell order");
    }
    if (quantity == 0) {
        throw std::invalid_argument("A trade quantity must be greater than zero");
    }

    records_.push_back({buyOrder.id(), sellOrder.id(), quantity, price});
}

const std::vector<Trade>& Trades::records() const noexcept {
    return records_;
}

std::size_t Trades::size() const noexcept {
    return records_.size();
}
