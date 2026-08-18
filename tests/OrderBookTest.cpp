#include "MatchingEngine.hpp"
#include "Order.hpp"
#include "OrderBook.hpp"

#include <memory>
#include <optional>
#include <stdexcept>

namespace {

std::unique_ptr<Order> makeOrder(
    Order::OrderId id,
    OrderAction action,
    Order::Quantity quantity,
    std::optional<Order::Price> price
) {
    return std::make_unique<Order>(id, action, quantity, price);
}

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("OrderBook test failed");
    }
}

} // namespace

int main() {
    MatchingEngine engine(std::make_unique<OrderBook>());

    engine.updateOrderBook(makeOrder(1, OrderAction::BUY, 100, 100));
    const auto& initialBids = engine.orderBook().getBidPriceLevels();
    require(initialBids.size() == 1);
    require(initialBids.at(100).totalQuantity() == 100);
    require(initialBids.at(100).topOrder().has_value());
    require(initialBids.at(100).topOrder()->get().id() == 1);
    require(engine.orderBook().order(1).state() == OrderState::ACTIVE);

    engine.updateOrderBook(makeOrder(2, OrderAction::SELL, 40, 90));
    const auto& partiallyFilledBids = engine.orderBook().getBidPriceLevels();
    require(partiallyFilledBids.at(100).totalQuantity() == 60);
    require(partiallyFilledBids.at(100).topOrder()->get().remainingQuantity() == 60);
    require(engine.orderBook().getAskPriceLevels().empty());
    require(engine.trades().size() == 1);
    require(engine.trades().records()[0].buyOrderId == 1);
    require(engine.trades().records()[0].sellOrderId == 2);
    require(engine.trades().records()[0].quantity == 40);
    require(engine.trades().records()[0].price == 100);

    engine.updateOrderBook(makeOrder(3, OrderAction::SELL, 80, 100));
    require(engine.orderBook().getBidPriceLevels().empty());
    const auto& asks = engine.orderBook().getAskPriceLevels();
    require(asks.size() == 1);
    require(asks.at(100).totalQuantity() == 20);
    require(asks.at(100).topOrder()->get().id() == 3);
    require(engine.trades().size() == 2);
    require(engine.trades().records()[1].buyOrderId == 1);
    require(engine.trades().records()[1].sellOrderId == 3);
    require(engine.trades().records()[1].quantity == 60);
    require(engine.trades().records()[1].price == 100);

    require(engine.orderBook().orderCount() == 3);
    require(engine.orderBook().order(1).state() == OrderState::FILLED);
    require(engine.orderBook().order(1).remainingQuantity() == 0);
    require(engine.orderBook().order(2).state() == OrderState::FILLED);
    require(engine.orderBook().order(3).state() == OrderState::ACTIVE);
    require(engine.orderBook().order(3).remainingQuantity() == 20);

    engine.updateOrderBook(makeOrder(3, OrderAction::RECALL, 0, std::nullopt));
    require(engine.orderBook().getAskPriceLevels().size() == 1);
    require(engine.orderBook().getAskPriceLevels().at(100).totalQuantity() == 0);
    require(!engine.orderBook().getAskPriceLevels().at(100).topOrder().has_value());
    require(engine.orderBook().orderCount() == 3);
    require(engine.orderBook().order(3).state() == OrderState::CANCELLED);
    require(engine.trades().size() == 2);

    engine.updateOrderBook(makeOrder(999, OrderAction::RECALL, 0, std::nullopt));
    engine.updateOrderBook(makeOrder(1, OrderAction::RECALL, 0, std::nullopt));
    require(engine.orderBook().orderCount() == 3);
    require(engine.trades().size() == 2);

    MatchingEngine cancellationEngine(std::make_unique<OrderBook>());
    cancellationEngine.updateOrderBook(makeOrder(20, OrderAction::BUY, 10, 100));
    cancellationEngine.updateOrderBook(makeOrder(21, OrderAction::BUY, 20, 100));
    cancellationEngine.updateOrderBook(makeOrder(
        20, OrderAction::RECALL, 0, std::nullopt
    ));
    require(cancellationEngine.orderBook().order(20).state() == OrderState::CANCELLED);
    require(cancellationEngine.orderBook().order(20).remainingQuantity() == 10);
    require(cancellationEngine.orderBook().getBidPriceLevels().at(100).totalQuantity() == 20);

    cancellationEngine.updateOrderBook(makeOrder(22, OrderAction::SELL, 15, 90));
    require(cancellationEngine.trades().size() == 1);
    require(cancellationEngine.trades().records()[0].buyOrderId == 21);
    require(cancellationEngine.trades().records()[0].sellOrderId == 22);
    require(cancellationEngine.trades().records()[0].quantity == 15);
    require(cancellationEngine.orderBook().order(20).state() == OrderState::CANCELLED);
    require(cancellationEngine.orderBook().order(20).remainingQuantity() == 10);
    require(cancellationEngine.orderBook().order(21).remainingQuantity() == 5);

    MatchingEngine cancelledLevelEngine(std::make_unique<OrderBook>());
    cancelledLevelEngine.updateOrderBook(makeOrder(30, OrderAction::SELL, 10, 100));
    cancelledLevelEngine.updateOrderBook(makeOrder(
        30, OrderAction::RECALL, 0, std::nullopt
    ));
    cancelledLevelEngine.updateOrderBook(makeOrder(31, OrderAction::BUY, 10, 100));
    require(cancelledLevelEngine.trades().size() == 0);
    require(cancelledLevelEngine.orderBook().order(30).state() == OrderState::CANCELLED);
    require(cancelledLevelEngine.orderBook().order(30).remainingQuantity() == 10);
    require(cancelledLevelEngine.orderBook().getAskPriceLevels().empty());
    require(cancelledLevelEngine.orderBook().getBidPriceLevels().at(100).totalQuantity() == 10);

    MatchingEngine multiLevelEngine(std::make_unique<OrderBook>());
    multiLevelEngine.updateOrderBook(makeOrder(10, OrderAction::SELL, 30, 100));
    multiLevelEngine.updateOrderBook(makeOrder(11, OrderAction::SELL, 40, 101));
    multiLevelEngine.updateOrderBook(makeOrder(12, OrderAction::SELL, 50, 101));
    multiLevelEngine.updateOrderBook(makeOrder(13, OrderAction::BUY, 100, 101));

    require(multiLevelEngine.trades().size() == 3);
    require(multiLevelEngine.trades().records()[0].sellOrderId == 10);
    require(multiLevelEngine.trades().records()[0].quantity == 30);
    require(multiLevelEngine.trades().records()[0].price == 100);
    require(multiLevelEngine.trades().records()[1].sellOrderId == 11);
    require(multiLevelEngine.trades().records()[1].quantity == 40);
    require(multiLevelEngine.trades().records()[1].price == 101);
    require(multiLevelEngine.trades().records()[2].sellOrderId == 12);
    require(multiLevelEngine.trades().records()[2].quantity == 30);
    require(multiLevelEngine.trades().records()[2].price == 101);

    require(multiLevelEngine.orderBook().orderCount() == 4);
    require(multiLevelEngine.orderBook().order(10).state() == OrderState::FILLED);
    require(multiLevelEngine.orderBook().order(11).state() == OrderState::FILLED);
    require(multiLevelEngine.orderBook().order(13).state() == OrderState::FILLED);
    require(multiLevelEngine.orderBook().order(12).remainingQuantity() == 20);
    require(multiLevelEngine.orderBook().getAskPriceLevels().at(101).topOrder()->get().id() == 12);
}
