#include "Strategy.hpp"
#include "Trades.hpp"

bool testStrategy::signal(const Trades& trades) const {
    uint64_t MA5 = 0;
    uint64_t MA30 = 0;
    uint64_t last_MA5 = 0;
    uint64_t last_MA30 = 0;

    const auto& records = trades.records();
    const size_t count = std::min<size_t>(records.size(), 30U);

    for (size_t i = records.size(); i-- > records.size() - count;) {
        const auto& trade = records[i];
        MA5 += trade.price;
        MA30 += trade.price;
    }

    MA5 /= 5;
    MA30 /= 30;
    return MA5 > MA30;
}