#pragma once

#include "Order.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct ClickHouseConfig {
    std::string url;
    std::string user;
    std::string password;
    std::string database;
    long connectTimeoutSeconds{10};
    long requestTimeoutSeconds{30};

    static ClickHouseConfig fromEnvironment();
};

struct ClickHouseColumn {
    std::string name;
    std::string type;
};

using ClickHouseValue = std::optional<std::string>;
using ClickHouseRow = std::vector<ClickHouseValue>;

struct ClickHouseResult {
    std::vector<ClickHouseColumn> columns;
    std::vector<ClickHouseRow> rows;

    const ClickHouseValue& value(
        std::size_t rowIndex,
        std::string_view columnName
    ) const;
};

// Exposed so the wire-format parser can be tested without a live server.
ClickHouseResult parseClickHouseTsv(std::string_view response);

class ClickHouseClient {
public:
    explicit ClickHouseClient(ClickHouseConfig config);

    ClickHouseResult query(std::string_view sql) const;
    ClickHouseResult listDatabases() const;
    ClickHouseResult listTables() const;
    ClickHouseResult listColumns() const;

private:
    ClickHouseConfig config_;
};

class BacktestCommissionsRepository {
public:
    BacktestCommissionsRepository(
        ClickHouseClient client,
        std::string tableName
    );

    ClickHouseResult fetch(std::size_t limit = 1'000) const;

private:
    ClickHouseClient client_;
    std::string quotedTableName_;
};

class EndOfOrderStream : public std::out_of_range {
public:
    EndOfOrderStream();
};

// Reads commission events in chronological order without loading the full
// backtest into memory. Each call returns one domain Order object.
class BacktestOrderStream {
public:
    BacktestOrderStream(
        ClickHouseClient client,
        std::string tableName,
        std::size_t pageSize = 1'000
    );

    Order nextOrder();
    bool exhausted() const noexcept;
    const std::optional<std::string>& lastDatetime() const noexcept;

private:
    void fetchNextPage();

    ClickHouseClient client_;
    std::string quotedTableName_;
    std::size_t pageSize_;
    ClickHouseResult page_;
    std::size_t nextRow_{0};
    std::optional<std::string> cursorDatetime_;
    std::optional<Order::OrderId> cursorId_;
    bool exhausted_{false};
};

// Public for callers that already have query results and for unit testing.
Order orderFromCommissionRow(
    const ClickHouseResult& result,
    std::size_t rowIndex
);
