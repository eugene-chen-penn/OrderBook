#include "ClickHouseClient.hpp"

#include <stdexcept>
#include <string>

namespace {

void require(bool condition) {
    if (!condition) {
        throw std::runtime_error("ClickHouseClient test failed");
    }
}

} // namespace

int main() {
    const std::string response =
        "backtest_id\tcommission\tnote\n"
        "UInt64\tNullable(Decimal(18, 8))\tString\n"
        "42\t0.00125000\tmaker\\tfee\n"
        "43\t\\N\tline one\\nline two\\x21\n";

    const ClickHouseResult result = parseClickHouseTsv(response);
    require(result.columns.size() == 3);
    require(result.columns[1].name == "commission");
    require(result.columns[1].type == "Nullable(Decimal(18, 8))");
    require(result.rows.size() == 2);
    require(result.value(0, "backtest_id") == "42");
    require(result.value(0, "note") == "maker\tfee");
    require(!result.value(1, "commission").has_value());
    require(result.value(1, "note") == "line one\nline two!");

    bool rejectedMalformedRow = false;
    try {
        static_cast<void>(parseClickHouseTsv("a\tb\nUInt8\tUInt8\n1\n"));
    } catch (const std::runtime_error&) {
        rejectedMalformedRow = true;
    }
    require(rejectedMalformedRow);

    bool rejectedMissingColumn = false;
    try {
        static_cast<void>(result.value(0, "does_not_exist"));
    } catch (const std::out_of_range&) {
        rejectedMissingColumn = true;
    }
    require(rejectedMissingColumn);

    const ClickHouseResult commissions = parseClickHouseTsv(
        "datetime\tcommission_price\tcommission_amount\tcommission_type\tid\n"
        "DateTime64(3)\tInt64\tInt64\tString\tInt64\n"
        "2026-01-05 09:15:00.000\t103800\t100\tB\t74\n"
        "2026-01-05 09:15:00.010\t115700\t11300\tS\t526\n"
    );

    const Order buy = orderFromCommissionRow(commissions, 0);
    require(buy.id() == 74);
    require(buy.action() == OrderAction::BUY);
    require(buy.remainingQuantity() == 100);
    require(buy.price() == 103800);

    const Order sell = orderFromCommissionRow(commissions, 1);
    require(sell.id() == 526);
    require(sell.action() == OrderAction::SELL);
    require(sell.remainingQuantity() == 11300);
    require(sell.price() == 115700);
}
