#pragma once

#include <cstdint>
#include <optional>

enum class OrderAction {
    BUY,
    SELL,
    RECALL
};

enum class OrderState {
    NEW,
    ACTIVE,
    FILLED,
    CANCELLED,
    REJECTED,
    EXPIRED
};

class Order {
public:
    using OrderId = std::uint64_t;
    using Quantity = std::uint64_t;
    using Price = std::int64_t;

    Order(
        OrderId id,
        OrderAction action,
        Quantity quantity,
        std::optional<Price> price
    );

    OrderId id() const;
    OrderAction action() const;
    OrderState state() const;
    Quantity originalQuantity() const;
    Quantity remainingQuantity() const;
    const std::optional<Price>& price() const;

    void fill(Quantity quantity);
    void activate();
    void cancel();

private:
    OrderId id_;
    OrderAction action_;
    OrderState state_;

    std::optional<Price> price_;

    Quantity originalQuantity_;
    Quantity remainingQuantity_;
};
