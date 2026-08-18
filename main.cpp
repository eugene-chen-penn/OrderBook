#include "ClickHouseClient.hpp"
#include "MatchingEngine.hpp"
#include "PriceGui.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct RunOptions {
    std::optional<std::size_t> maximumOrders;
    bool cacheTrades{false};
    bool keepGuiOpen{false};
};

void printTrade(const Trade& trade) {
    std::cout << "{\"buy_order_id\":" << trade.buyOrderId
              << ",\"sell_order_id\":" << trade.sellOrderId
              << ",\"quantity\":" << trade.quantity
              << ",\"price\":" << trade.price << "}\n";
}

std::size_t parseMaximumOrders(std::string_view input) {
    std::size_t maximumOrders{};
    const char* const begin = input.data();
    const char* const end = begin + input.size();
    const auto [position, error] = std::from_chars(begin, end, maximumOrders);
    if (error != std::errc{} || position != end || maximumOrders == 0) {
        throw std::invalid_argument("maximum orders must be a positive integer");
    }
    return maximumOrders;
}

RunOptions parseOptions(int argc, char* argv[]) {
    RunOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--cache-trades") {
            if (options.cacheTrades) {
                throw std::invalid_argument("--cache-trades was provided more than once");
            }
            options.cacheTrades = true;
        } else if (argument == "--keep-gui-open") {
            if (options.keepGuiOpen) {
                throw std::invalid_argument(
                    "--keep-gui-open was provided more than once"
                );
            }
            options.keepGuiOpen = true;
        } else if (!options.maximumOrders.has_value()) {
            options.maximumOrders = parseMaximumOrders(argument);
        } else {
            throw std::invalid_argument("too many arguments");
        }
    }
    if (options.cacheTrades && options.keepGuiOpen) {
        throw std::invalid_argument(
            "--keep-gui-open cannot be combined with --cache-trades"
        );
    }
    return options;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 4) {
            std::cerr
                << "Usage: orderbook [--cache-trades | --keep-gui-open] "
                << "[maximum-orders]\n";
            return 2;
        }
        const RunOptions options = parseOptions(argc, argv);

        ClickHouseConfig config = ClickHouseConfig::fromEnvironment();
        const char* configuredTable = std::getenv("CLICKHOUSE_COMMISSIONS_TABLE");
        const std::string table = configuredTable == nullptr
            ? "backtest_commissions"
            : configuredTable;

        BacktestOrderStream orderStream(
            ClickHouseClient(std::move(config)),
            table,
            options.cacheTrades ? 100'000 : 1'000
        );
        std::vector<std::unique_ptr<MatchingEngine>> completedSessions;
        auto engine = std::make_unique<MatchingEngine>(std::make_unique<OrderBook>());
        std::unique_ptr<PriceGui> priceGui;
        if (!options.cacheTrades) {
            priceGui = std::make_unique<PriceGui>();
            std::cerr << "Price GUI: " << priceGui->url() << '\n';
        }
        std::size_t processedOrders = 0;
        std::size_t sessionPrintedTrades = 0;
        std::size_t totalTrades = 0;
        std::string currentTradingDate;
        const auto startTime = std::chrono::steady_clock::now();

        try {
            while (!options.maximumOrders.has_value() ||
                   processedOrders < *options.maximumOrders) {
                Order order = orderStream.nextOrder();
                const std::string timestamp = orderStream.lastDatetime().value();
                const std::string tradingDate = timestamp.substr(0, 10);
                if (!currentTradingDate.empty() && tradingDate != currentTradingDate) {
                    completedSessions.push_back(std::move(engine));
                    engine = std::make_unique<MatchingEngine>(
                        std::make_unique<OrderBook>()
                    );
                    sessionPrintedTrades = 0;
                }
                currentTradingDate = tradingDate;

                engine->updateOrderBook(std::make_unique<Order>(std::move(order)));
                if (priceGui) {
                    priceGui->updateOrderBook(engine->orderBook());
                }
                ++processedOrders;

                const auto& records = engine->trades().records();
                if (options.cacheTrades) {
                    totalTrades += records.size() - sessionPrintedTrades;
                    sessionPrintedTrades = records.size();
                } else {
                    while (sessionPrintedTrades < records.size()) {
                        const Trade& trade = records[sessionPrintedTrades];
                        printTrade(trade);
                        priceGui->addTrade(trade, timestamp);
                        ++sessionPrintedTrades;
                        ++totalTrades;
                    }
                }
            }
        } catch (const EndOfOrderStream&) {
            // Normal end of the historical order stream.
        }

        const auto elapsed = std::chrono::steady_clock::now() - startTime;
        const double elapsedSeconds = std::chrono::duration<double>(elapsed).count();
        if (priceGui) {
            priceGui->completeReplay();
        }
        std::cerr << "Processed " << processedOrders << " order(s) and generated "
                  << totalTrades << " trade(s) across "
                  << completedSessions.size() + 1 << " trading session(s).\n";
        if (options.cacheTrades) {
            const double ordersPerSecond = elapsedSeconds > 0.0
                ? static_cast<double>(processedOrders) / elapsedSeconds
                : 0.0;
            std::cerr << "Cached-trades run: " << elapsedSeconds << " seconds, "
                      << ordersPerSecond << " orders/second.\n";
        }
        if (priceGui && options.keepGuiOpen) {
            std::cerr << "GUI complete. Press Enter to close it.\n";
            std::string input;
            std::getline(std::cin, input);
        }
        if (priceGui) {
            priceGui->finish();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "orderbook: " << error.what() << '\n';
        return 1;
    }
}
