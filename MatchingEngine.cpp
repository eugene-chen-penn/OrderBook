#include "MatchingEngine.hpp"

#include <stdexcept>
#include <utility>

MatchingEngine::MatchingEngine(std::unique_ptr<OrderBook> orderBook)
    : orderBook_(std::move(orderBook))
{
    if (!orderBook_) {
        throw std::invalid_argument("MatchingEngine requires an order book");
    }
}

void MatchingEngine::updateOrderBook(std::unique_ptr<Order> order) {
    if (!order) {
        throw std::invalid_argument("Cannot process a null order");
    }

    if (order->action() == OrderAction::RECALL) {
        static_cast<void>(orderBook_->recallOrder(order->id()));
        return;
    }

    orderBook_->fillAtOrBetterPrice(std::move(order), trades_);
}

OrderBook& MatchingEngine::orderBook() noexcept {
    return *orderBook_;
}

const OrderBook& MatchingEngine::orderBook() const noexcept {
    return *orderBook_;
}

const Trades& MatchingEngine::trades() const noexcept {
    return trades_;
}
