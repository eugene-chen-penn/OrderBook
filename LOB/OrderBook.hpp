#pragma once

#include "Order.hpp"
#include "PriceLevel.hpp"
#include "Trades.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <unordered_map>

class OrderBook {
public:
    using PriceLevels = std::map<Order::Price, PriceLevel>;
    using Orders = std::unordered_map<Order::OrderId, std::unique_ptr<Order>>;

    OrderBook() = default;
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) noexcept = default;
    OrderBook& operator=(OrderBook&&) noexcept = default;

    // Ownership of the order is transferred to the book.
    void addOrder(std::unique_ptr<Order> order);
    void fillAtOrBetterPrice(std::unique_ptr<Order> order, Trades& trades);
    bool recallOrder(Order::OrderId orderId);

    const Order& order(Order::OrderId orderId) const;
    std::size_t orderCount() const noexcept;
    const PriceLevels& getBidPriceLevels() const noexcept;
    const PriceLevels& getAskPriceLevels() const noexcept;

private:
    Order& logOrder(std::unique_ptr<Order> order);
    Order& findOrder(Order::OrderId orderId);
    const Order& findOrder(Order::OrderId orderId) const;
    void addToPriceLevel(Order& order);

    // Declared first so the owned orders outlive the non-owning price-level
    // references during destruction.
    Orders allOrders_;
    PriceLevels bidPriceLevels_;
    PriceLevels askPriceLevels_;
};
