#include "OrderBook.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

void validateLimitOrder(const Order& order) {
    if (order.action() != OrderAction::BUY && order.action() != OrderAction::SELL) {
        throw std::invalid_argument("Only buy and sell orders can enter the book");
    }
    if (!order.price().has_value()) {
        throw std::invalid_argument("A buy or sell order must have a price");
    }
    if (order.originalQuantity() == 0) {
        throw std::invalid_argument("A buy or sell order must have a quantity");
    }
}

} // namespace

Order& OrderBook::logOrder(std::unique_ptr<Order> order) {
    if (!order) {
        throw std::invalid_argument("Cannot add a null order");
    }

    const Order::OrderId orderId = order->id();
    const auto [position, inserted] = allOrders_.emplace(orderId, std::move(order));
    if (!inserted) {
        throw std::invalid_argument(
            "An order with ID " + std::to_string(orderId) + " already exists"
        );
    }
    return *position->second;
}

void OrderBook::addToPriceLevel(Order& order) {
    validateLimitOrder(order);
    const Order::Price price = order.price().value();
    PriceLevels& priceLevels = order.action() == OrderAction::BUY
        ? bidPriceLevels_
        : askPriceLevels_;

    const auto position = priceLevels.find(price);
    if (position != priceLevels.end()) {
        position->second.addOrder(order);
    } else {
        priceLevels.emplace(price, PriceLevel(price, order));
    }
}

void OrderBook::addOrder(std::unique_ptr<Order> order) {
    if (!order) {
        throw std::invalid_argument("Cannot add a null order");
    }
    validateLimitOrder(*order);
    if (order->state() != OrderState::NEW) {
        throw std::invalid_argument("Only a new order can enter the book");
    }

    Order& storedOrder = logOrder(std::move(order));
    storedOrder.activate();
    addToPriceLevel(storedOrder);
}

Order& OrderBook::findOrder(Order::OrderId orderId) {
    const auto position = allOrders_.find(orderId);
    if (position == allOrders_.end()) {
        throw std::invalid_argument("Order not found in order book");
    }
    return *position->second;
}

const Order& OrderBook::findOrder(Order::OrderId orderId) const {
    const auto position = allOrders_.find(orderId);
    if (position == allOrders_.end()) {
        throw std::invalid_argument("Order not found in order book");
    }
    return *position->second;
}

bool OrderBook::recallOrder(Order::OrderId orderId) {
    const auto orderPosition = allOrders_.find(orderId);
    if (orderPosition == allOrders_.end()) {
        return false;
    }

    Order& order = *orderPosition->second;
    if (order.state() != OrderState::ACTIVE || !order.price().has_value()) {
        return false;
    }

    PriceLevels& priceLevels = order.action() == OrderAction::BUY
        ? bidPriceLevels_
        : askPriceLevels_;
    const auto position = priceLevels.find(order.price().value());
    if (position == priceLevels.end() || !position->second.removeOrder(orderId)) {
        return false;
    }
    return true;
}

const OrderBook::PriceLevels& OrderBook::getBidPriceLevels() const noexcept {
    return bidPriceLevels_;
}

const OrderBook::PriceLevels& OrderBook::getAskPriceLevels() const noexcept {
    return askPriceLevels_;
}

const Order& OrderBook::order(Order::OrderId orderId) const {
    return findOrder(orderId);
}

std::size_t OrderBook::orderCount() const noexcept {
    return allOrders_.size();
}

void OrderBook::fillAtOrBetterPrice(
    std::unique_ptr<Order> order,
    Trades& trades
) {
    if (!order) {
        throw std::invalid_argument("Cannot match a null order");
    }
    validateLimitOrder(*order);
    if (order->state() != OrderState::NEW) {
        throw std::invalid_argument("Only a new order can enter the book");
    }

    Order& incomingOrder = logOrder(std::move(order));
    incomingOrder.activate();
    const Order::Price limitPrice = incomingOrder.price().value();

    while (incomingOrder.remainingQuantity() > 0) {
        if (incomingOrder.action() == OrderAction::BUY) {
            const auto position = askPriceLevels_.begin();
            if (position == askPriceLevels_.end() || position->first > limitPrice) {
                addToPriceLevel(incomingOrder);
                return;
            }

            PriceLevel& priceLevel = position->second;
            const auto restingOrderReference = priceLevel.topOrder();
            if (!restingOrderReference.has_value()) {
                askPriceLevels_.erase(position);
                continue;
            }
            Order& restingOrder = restingOrderReference->get();
            const Order::Quantity fillQuantity = std::min(
                incomingOrder.remainingQuantity(),
                restingOrder.remainingQuantity()
            );
            priceLevel.fillTopOrder(fillQuantity);
            incomingOrder.fill(fillQuantity);
            trades.log(incomingOrder, restingOrder, fillQuantity, priceLevel.price());
            if (priceLevel.totalQuantity() == 0) {
                askPriceLevels_.erase(position);
            }
        } else {
            const auto reversePosition = bidPriceLevels_.rbegin();
            if (reversePosition == bidPriceLevels_.rend() ||
                reversePosition->first < limitPrice) {
                addToPriceLevel(incomingOrder);
                return;
            }

            PriceLevel& priceLevel = reversePosition->second;
            const auto restingOrderReference = priceLevel.topOrder();
            if (!restingOrderReference.has_value()) {
                bidPriceLevels_.erase(std::next(reversePosition).base());
                continue;
            }
            Order& restingOrder = restingOrderReference->get();
            const Order::Quantity fillQuantity = std::min(
                incomingOrder.remainingQuantity(),
                restingOrder.remainingQuantity()
            );
            priceLevel.fillTopOrder(fillQuantity);
            incomingOrder.fill(fillQuantity);
            trades.log(restingOrder, incomingOrder, fillQuantity, priceLevel.price());
            if (priceLevel.totalQuantity() == 0) {
                bidPriceLevels_.erase(std::next(reversePosition).base());
            }
        }
    }
}
