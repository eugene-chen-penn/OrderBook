#include "ClickHouseClient.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::size_t parseLimit(std::string_view input) {
    std::size_t limit{};
    const char* const begin = input.data();
    const char* const end = begin + input.size();
    const auto [position, error] = std::from_chars(begin, end, limit);
    if (error != std::errc{} || position != end || limit == 0) {
        throw std::runtime_error("limit must be a positive integer");
    }
    return limit;
}

void printJsonString(std::string_view value) {
    std::cout << '"';
    for (const char rawCharacter : value) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        switch (character) {
        case '"': std::cout << "\\\""; break;
        case '\\': std::cout << "\\\\"; break;
        case '\b': std::cout << "\\b"; break;
        case '\f': std::cout << "\\f"; break;
        case '\n': std::cout << "\\n"; break;
        case '\r': std::cout << "\\r"; break;
        case '\t': std::cout << "\\t"; break;
        default:
            if (character < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                std::cout << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
            } else {
                std::cout << static_cast<char>(character);
            }
        }
    }
    std::cout << '"';
}

void printResult(const ClickHouseResult& result) {
    for (const ClickHouseRow& row : result.rows) {
        std::cout << '{';
        for (std::size_t index = 0; index < result.columns.size(); ++index) {
            if (index != 0) {
                std::cout << ',';
            }
            printJsonString(result.columns[index].name);
            std::cout << ':';
            if (row[index].has_value()) {
                printJsonString(*row[index]);
            } else {
                std::cout << "null";
            }
        }
        std::cout << "}\n";
    }
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc > 2) {
            std::cerr
                << "Usage: backtest_commissions "
                << "[limit | --list-databases | --list-tables | --list-columns]\n";
            return 2;
        }

        ClickHouseConfig config = ClickHouseConfig::fromEnvironment();
        if (argc == 2 && std::string_view(argv[1]) == "--list-databases") {
            const ClickHouseResult result =
                ClickHouseClient(std::move(config)).listDatabases();
            printResult(result);
            std::cerr << "Found " << result.rows.size() << " database(s).\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--list-tables") {
            const ClickHouseResult result = ClickHouseClient(std::move(config)).listTables();
            printResult(result);
            std::cerr << "Found " << result.rows.size() << " table(s).\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--list-columns") {
            const ClickHouseResult result = ClickHouseClient(std::move(config)).listColumns();
            printResult(result);
            std::cerr << "Found " << result.rows.size() << " column(s).\n";
            return 0;
        }

        const std::size_t limit = argc == 2 ? parseLimit(argv[1]) : 100;
        const char* configuredTable = std::getenv("CLICKHOUSE_COMMISSIONS_TABLE");
        const std::string table = configuredTable == nullptr
            ? "backtest_commissions"
            : configuredTable;

        BacktestCommissionsRepository repository(
            ClickHouseClient(std::move(config)),
            table
        );
        const ClickHouseResult result = repository.fetch(limit);

        printResult(result);

        std::cerr << "Fetched " << result.rows.size() << " commission row(s).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "backtest_commissions: " << error.what() << '\n';
        return 1;
    }
}
