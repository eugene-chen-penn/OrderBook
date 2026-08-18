#pragma once

#include "Order.hpp"
#include "OrderBook.hpp"
#include "Trades.hpp"

#include <memory>

class MatchingEngine {
public:
    explicit MatchingEngine(std::unique_ptr<OrderBook> orderBook);

    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;
    MatchingEngine(MatchingEngine&&) noexcept = default;
    MatchingEngine& operator=(MatchingEngine&&) noexcept = default;

    void updateOrderBook(std::unique_ptr<Order> order);

    OrderBook& orderBook() noexcept;
    const OrderBook& orderBook() const noexcept;
    const Trades& trades() const noexcept;

private:
    std::unique_ptr<OrderBook> orderBook_;
    Trades trades_;
};
