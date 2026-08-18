#pragma once

#include "OrderBook.hpp"
#include "Trades.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class PriceGui {
public:
    explicit PriceGui(
        std::chrono::milliseconds interval = std::chrono::milliseconds(100)
    );
    ~PriceGui();

    PriceGui(const PriceGui&) = delete;
    PriceGui& operator=(const PriceGui&) = delete;

    void addTrade(const Trade& trade, std::string_view timestamp);
    void updateOrderBook(const OrderBook& orderBook);
    void completeReplay();
    void finish();
    const std::string& url() const noexcept;

private:
    struct Sample {
        std::int64_t timeMilliseconds;
        std::string timestamp;
        Order::Price closingPrice;
        Order::Quantity quantity;
    };

    struct BookLevel {
        Order::Price price;
        Order::Quantity quantity;
    };

    void flushPendingSample();
    void serverLoop();
    std::string dataJson(std::size_t afterSample);

    std::chrono::milliseconds interval_;
    int listenSocket_{-1};
    std::string url_;
    bool stopped_{false};
    bool stopRequested_{false};

    std::mutex mutex_;
    std::thread server_;

    std::optional<std::int64_t> pendingBucketMilliseconds_;
    std::optional<Order::Price> pendingClosingPrice_;
    std::optional<std::string> pendingTimestamp_;
    Order::Quantity pendingQuantity_{0};
    std::vector<Sample> samples_;
    std::vector<BookLevel> bids_;
    std::vector<BookLevel> asks_;
};
