# OrderBook

## Toolchain

- C++ standard: C++20
- Compiler: Apple Clang (`/usr/bin/clang++`)
- Build system: CMake 3.25 or newer
- Architecture on this Mac: Apple Silicon (`arm64`)

The checked-in CMake presets make command-line and VS Code builds use the same settings.

## VS Code

The required **C/C++** and **CMake Tools** extensions are already installed on this Mac.

1. Open this folder in VS Code.
2. If CMake Tools asks for a configure preset, select **Debug (Apple Clang)**.
3. Press **Command+Shift+B** to configure and build.
4. Press **F5** to build and debug with LLDB.
5. Run the `Run orderbook` task to build and run without the debugger.

The debugger and `Run orderbook` task load ClickHouse settings from `.env`.

## Terminal

```sh
export CLICKHOUSE_URL='https://clickhouse.example.com:8443'
export CLICKHOUSE_USER='readonly_backtests'
export CLICKHOUSE_PASSWORD='replace-me'
export CLICKHOUSE_DATABASE='analytics'
export CLICKHOUSE_COMMISSIONS_TABLE='backtest_commissions'

cmake --preset debug
cmake --build --preset debug
./build/debug/orderbook
```

The `orderbook` executable streams the configured ClickHouse table in
chronological order, runs each row through the matching engine, and writes each
generated trade to standard output as one JSON object per line. Progress and
errors are written to standard error. For an optimized build, replace `debug`
with `release`.

Because the source order IDs restart on each trading date, the executable uses
one matching engine per date. Completed session engines are retained in memory,
so their filled and cancelled orders remain in their respective `allOrders_`
collections and orders from different dates can never match each other.

The executable opens a local browser GUI that polls for updates every 100 ms.
Trades are grouped into 100 ms intervals using their ClickHouse market
timestamps. `Latest close` is the last executed price in the market-time
interval and `Quantity / 100 ms` is the sum of executed quantities in that interval. The
chart keeps every sample from program startup, so its first point remains
visible instead of the chart rolling forward. The x-axis uses ClickHouse market
timestamps, and displayed prices are the stored integer price divided by 10,000.
A classic depth ladder shows five asks above the spread and five bids below it,
with the best prices nearest the center and the total quantity at each level.
The terminal continues to emit JSON trade records and the local GUI URL:

```sh
./build/debug/orderbook > trades.jsonl
```

Pass an optional positive maximum-order count for a bounded run, for example:

```sh
./build/debug/orderbook 1000
```

To run without the browser GUI or JSON trade output, while retaining every
trade in memory, use cached-trades mode. Its summary reports elapsed wall-clock
time and order throughput to standard error:

```sh
./build/release/orderbook --cache-trades
./build/release/orderbook --cache-trades 100000
```

To leave a completed visualization open until Enter is pressed, run:

```sh
./build/release/orderbook --keep-gui-open 100324
```

## Matching orders

`MatchingEngine` accepts owned orders and keeps every buy and sell in its
`OrderBook`, including filled and cancelled orders. Each buyer/seller execution
is appended to `Trades`; a partially filled order can therefore appear in more
than one trade record.

Recalling an order marks the stored `Order` as `CANCELLED` without removing its
queue entry. Matching and the GUI depth ladder ignore cancelled orders; stale
queue entries are discarded lazily when matching reaches them.

```cpp
MatchingEngine engine(std::make_unique<OrderBook>());
engine.updateOrderBook(
    std::make_unique<Order>(1, OrderAction::BUY, 100, 101)
);
engine.updateOrderBook(
    std::make_unique<Order>(2, OrderAction::SELL, 40, 100)
);

const Trade& trade = engine.trades().records().front();
const Order& buy = engine.orderBook().order(1);
```

## Backtest commissions from ClickHouse

The `backtest_commissions` executable reads commission rows through ClickHouse's
HTTP interface. It uses HTTP Basic authentication and keeps all connection
details outside the source code.

Copy the settings from `.env.example` into your shell environment (or a local
`.env` file that your shell tooling loads), then build and run:

```sh
export CLICKHOUSE_URL='https://clickhouse.example.com:8443'
export CLICKHOUSE_USER='readonly_backtests'
export CLICKHOUSE_PASSWORD='replace-me'
export CLICKHOUSE_DATABASE='analytics'
export CLICKHOUSE_COMMISSIONS_TABLE='backtest_commissions'

cmake --preset debug
cmake --build --preset debug
./build/debug/backtest_commissions 100
```

The optional argument is the maximum number of rows (default: `100`). Output is
JSON Lines, with values represented as strings so ClickHouse decimal values do
not lose precision. Nullable values are emitted as JSON `null`.

Application code can use the same repository directly:

```cpp
ClickHouseConfig config = ClickHouseConfig::fromEnvironment();
BacktestCommissionsRepository commissions(
    ClickHouseClient(std::move(config)),
    "backtest_commissions"
);

ClickHouseResult result = commissions.fetch(1'000);
for (std::size_t row = 0; row < result.rows.size(); ++row) {
    const ClickHouseValue& amount = result.value(row, "commission");
    // Convert according to your table's exact Decimal type and scale.
}
```

The configured ClickHouse user only needs `SELECT` access to the commissions
table. HTTPS certificate verification remains enabled by libcurl.

### Stream orders chronologically

`BacktestOrderStream` converts commission rows into `Order` objects one at a
time. Rows are ordered by `datetime`, then `id`; data is fetched in pages so the
entire backtest is never held in memory.

```cpp
#include "ClickHouseClient.hpp"

ClickHouseConfig config = ClickHouseConfig::fromEnvironment();
BacktestOrderStream orders(
    ClickHouseClient(std::move(config)),
    "market_data",
    10'000 // Rows per network request; default is 1,000.
);

try {
    while (true) {
        Order order = orders.nextOrder();
        // Process order here.
    }
} catch (const EndOfOrderStream&) {
    // Normal end of the backtest.
}
```

The mapping is `id` to `Order::id`, `commission_amount` to quantity,
`commission_price` to price, and `B`/`S` to `BUY`/`SELL`. `R` and `C` are
mapped to `RECALL`.

To list the tables in `CLICKHOUSE_DATABASE` using the same connection:

```sh
./build/debug/backtest_commissions --list-tables
```

If you do not yet know the database name, first run:

```sh
./build/debug/backtest_commissions --list-databases
```

To inspect the columns of every visible table in the configured database:

```sh
./build/debug/backtest_commissions --list-columns
```

This reports each table's name, engine, approximate row count, and approximate
stored-byte count. The equivalent SQL is:

```sql
SELECT name, engine, total_rows, total_bytes
FROM system.tables
WHERE database = currentDatabase()
ORDER BY name;
```



## Architecture
main.cpp
 ├─ includes OrderBook.hpp
 │    └─ includes Order.hpp
 └─ includes Order.hpp


 Order
    stores one order

PriceLevel
    stores FIFO orders at one price

OrderBook
    stores bids/asks and finds price levels

MatchingEngine
    determines who trades with whom

Trades
    stores one record for every buyer/seller execution

## Ownership

- `MatchingEngine` uniquely owns its `OrderBook`.
- `OrderBook` uniquely owns every submitted `Order`.
- `PriceLevel` stores non-owning `std::reference_wrapper<Order>` entries in
  FIFO order. The references remain valid because their `Order` objects are
  heap-allocated and retained by the book for the book's lifetime.
- Submit orders with `std::make_unique<Order>(...)` and transfer ownership with
  `std::move` when passing an existing `std::unique_ptr<Order>`.
