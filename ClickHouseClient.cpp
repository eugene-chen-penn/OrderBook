#include "ClickHouseClient.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

std::string environmentValue(const char* name, std::string fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return fallback;
    }
    return value;
}

long positiveLongFromEnvironment(const char* name, long fallback) {
    const std::string value = environmentValue(name, "");
    if (value.empty()) {
        return fallback;
    }

    long parsed{};
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto [position, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || position != end || parsed <= 0) {
        throw std::runtime_error(std::string(name) + " must be a positive integer");
    }
    return parsed;
}

void rejectHeaderNewlines(std::string_view value, std::string_view name) {
    if (value.find_first_of("\r\n") != std::string_view::npos) {
        throw std::runtime_error(std::string(name) + " cannot contain a newline");
    }
}

bool isIdentifierStart(char value) {
    return (value >= 'a' && value <= 'z') ||
        (value >= 'A' && value <= 'Z') || value == '_';
}

bool isIdentifierContinuation(char value) {
    return isIdentifierStart(value) || (value >= '0' && value <= '9');
}

std::string quoteQualifiedIdentifier(std::string_view identifier) {
    if (identifier.empty()) {
        throw std::runtime_error("CLICKHOUSE_COMMISSIONS_TABLE cannot be empty");
    }
    if (identifier.back() == '.') {
        throw std::runtime_error(
            "CLICKHOUSE_COMMISSIONS_TABLE must not end with a dot"
        );
    }

    std::string quoted;
    std::size_t componentStart = 0;
    while (componentStart < identifier.size()) {
        const std::size_t dot = identifier.find('.', componentStart);
        const std::size_t componentEnd =
            dot == std::string_view::npos ? identifier.size() : dot;
        const std::string_view component = identifier.substr(
            componentStart,
            componentEnd - componentStart
        );

        if (component.empty() || !isIdentifierStart(component.front()) ||
            !std::all_of(
                component.begin() + 1,
                component.end(),
                isIdentifierContinuation
            )) {
            throw std::runtime_error(
                "CLICKHOUSE_COMMISSIONS_TABLE must be a table name or database.table "
                "using letters, digits, and underscores"
            );
        }

        if (!quoted.empty()) {
            quoted.push_back('.');
        }
        quoted.push_back('`');
        quoted.append(component);
        quoted.push_back('`');

        if (dot == std::string_view::npos) {
            break;
        }
        componentStart = dot + 1;
    }

    return quoted;
}

std::vector<std::string_view> splitLines(std::string_view response) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < response.size()) {
        const std::size_t newline = response.find('\n', start);
        const std::size_t end =
            newline == std::string_view::npos ? response.size() : newline;
        std::string_view line = response.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return lines;
}

std::vector<std::string_view> splitFields(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

int hexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

std::string unescapeTsv(std::string_view field) {
    std::string value;
    value.reserve(field.size());
    for (std::size_t index = 0; index < field.size(); ++index) {
        const char current = field[index];
        if (current != '\\' || index + 1 >= field.size()) {
            value.push_back(current);
            continue;
        }

        const char escaped = field[++index];
        switch (escaped) {
        case 'a': value.push_back('\a'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'v': value.push_back('\v'); break;
        case '0': value.push_back('\0'); break;
        case '\'': value.push_back('\''); break;
        case '\\': value.push_back('\\'); break;
        case 'x': {
            if (index + 2 < field.size()) {
                const int high = hexDigit(field[index + 1]);
                const int low = hexDigit(field[index + 2]);
                if (high >= 0 && low >= 0) {
                    value.push_back(static_cast<char>((high << 4) | low));
                    index += 2;
                    break;
                }
            }
            value.push_back('x');
            break;
        }
        default:
            value.push_back(escaped);
            break;
        }
    }
    return value;
}

std::size_t writeResponse(
    char* data,
    std::size_t elementSize,
    std::size_t elementCount,
    void* context
) {
    const std::size_t byteCount = elementSize * elementCount;
    auto* const response = static_cast<std::string*>(context);
    response->append(data, byteCount);
    return byteCount;
}

struct CurlHandleDeleter {
    void operator()(CURL* handle) const {
        curl_easy_cleanup(handle);
    }
};

struct CurlHeadersDeleter {
    void operator()(curl_slist* headers) const {
        curl_slist_free_all(headers);
    }
};

using CurlHandle = std::unique_ptr<CURL, CurlHandleDeleter>;
using CurlHeaders = std::unique_ptr<curl_slist, CurlHeadersDeleter>;

void appendHeader(CurlHeaders& headers, const char* header) {
    curl_slist* const original = headers.get();
    curl_slist* const updated = curl_slist_append(original, header);
    if (updated == nullptr) {
        throw std::runtime_error("Could not allocate HTTP headers");
    }

    if (updated != original) {
        static_cast<void>(headers.release());
        headers.reset(updated);
    }
}

void ensureCurlInitialized() {
    static const bool initialized = [] {
        return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }();
    if (!initialized) {
        throw std::runtime_error("Could not initialize libcurl");
    }
}

void setCurlOption(CURL* handle, CURLoption option, long value) {
    const CURLcode result = curl_easy_setopt(handle, option, value);
    if (result != CURLE_OK) {
        throw std::runtime_error(curl_easy_strerror(result));
    }
}

void setCurlOption(CURL* handle, CURLoption option, const char* value) {
    const CURLcode result = curl_easy_setopt(handle, option, value);
    if (result != CURLE_OK) {
        throw std::runtime_error(curl_easy_strerror(result));
    }
}

bool isRetryableCurlError(CURLcode error) {
    return error == CURLE_COULDNT_CONNECT ||
        error == CURLE_OPERATION_TIMEDOUT ||
        error == CURLE_RECV_ERROR ||
        error == CURLE_GOT_NOTHING;
}

bool isSelectQuery(std::string_view sql) {
    const std::size_t firstCharacter = sql.find_first_not_of(" \t\r\n");
    return firstCharacter != std::string_view::npos &&
        sql.substr(firstCharacter).starts_with("SELECT");
}

const std::string& requiredValue(
    const ClickHouseResult& result,
    std::size_t rowIndex,
    std::string_view columnName
) {
    const ClickHouseValue& value = result.value(rowIndex, columnName);
    if (!value.has_value()) {
        throw std::runtime_error(
            "ClickHouse column '" + std::string(columnName) + "' cannot be NULL"
        );
    }
    return *value;
}

template <typename Number>
Number parseNumber(std::string_view text, std::string_view columnName) {
    Number value{};
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto [position, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || position != end) {
        throw std::runtime_error(
            "Invalid numeric value in ClickHouse column '" +
            std::string(columnName) + "': " + std::string(text)
        );
    }
    return value;
}

std::string quoteSqlString(std::string_view value) {
    std::string quoted{"'"};
    quoted.reserve(value.size() + 2);
    for (const char character : value) {
        if (character == '\\' || character == '\'') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('\'');
    return quoted;
}

} // namespace

ClickHouseConfig ClickHouseConfig::fromEnvironment() {
    ClickHouseConfig config{
        .url = environmentValue("CLICKHOUSE_URL", "http://localhost:8123"),
        .user = environmentValue("CLICKHOUSE_USER", "default"),
        .password = environmentValue("CLICKHOUSE_PASSWORD", ""),
        .database = environmentValue("CLICKHOUSE_DATABASE", "default"),
        .connectTimeoutSeconds = positiveLongFromEnvironment(
            "CLICKHOUSE_CONNECT_TIMEOUT_SECONDS",
            10
        ),
        .requestTimeoutSeconds = positiveLongFromEnvironment(
            "CLICKHOUSE_REQUEST_TIMEOUT_SECONDS",
            30
        )
    };

    if (config.url.empty()) {
        throw std::runtime_error("CLICKHOUSE_URL cannot be empty");
    }
    rejectHeaderNewlines(config.database, "CLICKHOUSE_DATABASE");
    return config;
}

const ClickHouseValue& ClickHouseResult::value(
    std::size_t rowIndex,
    std::string_view columnName
) const {
    if (rowIndex >= rows.size()) {
        throw std::out_of_range("ClickHouse row index is out of range");
    }

    const auto column = std::find_if(
        columns.begin(),
        columns.end(),
        [columnName](const ClickHouseColumn& candidate) {
            return candidate.name == columnName;
        }
    );
    if (column == columns.end()) {
        throw std::out_of_range("ClickHouse result does not contain that column");
    }

    const auto index = static_cast<std::size_t>(column - columns.begin());
    return rows[rowIndex][index];
}

ClickHouseResult parseClickHouseTsv(std::string_view response) {
    const auto lines = splitLines(response);
    if (lines.size() < 2) {
        throw std::runtime_error(
            "ClickHouse returned an incomplete TabSeparatedWithNamesAndTypes response"
        );
    }

    const auto names = splitFields(lines[0]);
    const auto types = splitFields(lines[1]);
    if (names.size() != types.size()) {
        throw std::runtime_error("ClickHouse returned mismatched column names and types");
    }

    ClickHouseResult result;
    result.columns.reserve(names.size());
    for (std::size_t index = 0; index < names.size(); ++index) {
        result.columns.push_back({unescapeTsv(names[index]), unescapeTsv(types[index])});
    }

    result.rows.reserve(lines.size() - 2);
    for (std::size_t lineIndex = 2; lineIndex < lines.size(); ++lineIndex) {
        const auto fields = splitFields(lines[lineIndex]);
        if (fields.size() != names.size()) {
            throw std::runtime_error(
                "ClickHouse returned a row with an unexpected number of columns"
            );
        }

        ClickHouseRow row;
        row.reserve(fields.size());
        for (const std::string_view field : fields) {
            if (field == R"(\N)") {
                row.emplace_back(std::nullopt);
            } else {
                row.emplace_back(unescapeTsv(field));
            }
        }
        result.rows.push_back(std::move(row));
    }

    return result;
}

ClickHouseClient::ClickHouseClient(ClickHouseConfig config)
    : config_(std::move(config)) {
}

ClickHouseResult ClickHouseClient::query(std::string_view sql) const {
    ensureCurlInitialized();

    CurlHandle handle(curl_easy_init());
    if (!handle) {
        throw std::runtime_error("Could not create a libcurl request");
    }

    std::string url = config_.url;
    url += url.find('?') == std::string::npos ? "?wait_end_of_query=1" : "&wait_end_of_query=1";
    std::string query(sql);
    std::string response;

    CurlHeaders headers;
    appendHeader(headers, "Content-Type: text/plain; charset=utf-8");
    const std::string databaseHeader = "X-ClickHouse-Database: " + config_.database;
    appendHeader(headers, databaseHeader.c_str());

    setCurlOption(handle.get(), CURLOPT_URL, url.c_str());
    setCurlOption(handle.get(), CURLOPT_USERNAME, config_.user.c_str());
    setCurlOption(handle.get(), CURLOPT_PASSWORD, config_.password.c_str());
    setCurlOption(handle.get(), CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    setCurlOption(handle.get(), CURLOPT_POST, 1L);
    setCurlOption(handle.get(), CURLOPT_POSTFIELDS, query.c_str());
    curl_easy_setopt(
        handle.get(),
        CURLOPT_POSTFIELDSIZE_LARGE,
        static_cast<curl_off_t>(query.size())
    );
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, writeResponse);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);
    setCurlOption(handle.get(), CURLOPT_CONNECTTIMEOUT, config_.connectTimeoutSeconds);
    setCurlOption(handle.get(), CURLOPT_TIMEOUT, config_.requestTimeoutSeconds);
    setCurlOption(handle.get(), CURLOPT_NOSIGNAL, 1L);
    setCurlOption(handle.get(), CURLOPT_TCP_KEEPALIVE, 1L);
    setCurlOption(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
    setCurlOption(handle.get(), CURLOPT_ACCEPT_ENCODING, "");
    setCurlOption(handle.get(), CURLOPT_USERAGENT, "OrderBook/0.1 ClickHouseClient");

    constexpr int maximumAttempts = 3;
    const int allowedAttempts = isSelectQuery(sql) ? maximumAttempts : 1;
    CURLcode requestResult = CURLE_OK;
    for (int attempt = 1; attempt <= allowedAttempts; ++attempt) {
        response.clear();
        if (attempt > 1) {
            setCurlOption(handle.get(), CURLOPT_FRESH_CONNECT, 1L);
            std::this_thread::sleep_for(std::chrono::milliseconds(250 * attempt));
        }

        requestResult = curl_easy_perform(handle.get());
        if (requestResult == CURLE_OK || !isRetryableCurlError(requestResult)) {
            break;
        }
    }
    if (requestResult != CURLE_OK) {
        throw std::runtime_error(
            std::string("ClickHouse request failed after ") +
            std::to_string(allowedAttempts) + " attempt(s): " +
            curl_easy_strerror(requestResult)
        );
    }

    long statusCode{};
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &statusCode);
    if (statusCode < 200 || statusCode >= 300) {
        throw std::runtime_error(
            "ClickHouse returned HTTP " + std::to_string(statusCode) + ": " + response
        );
    }
    if (response.find("\n__exception__\n") != std::string::npos) {
        throw std::runtime_error("ClickHouse query failed: " + response);
    }

    return parseClickHouseTsv(response);
}

ClickHouseResult ClickHouseClient::listTables() const {
    return query(
        "SELECT name, engine, total_rows, total_bytes "
        "FROM system.tables "
        "WHERE database = currentDatabase() "
        "ORDER BY name "
        "FORMAT TabSeparatedWithNamesAndTypes"
    );
}

ClickHouseResult ClickHouseClient::listDatabases() const {
    return query(
        "SELECT name, engine "
        "FROM system.databases "
        "ORDER BY name "
        "FORMAT TabSeparatedWithNamesAndTypes"
    );
}

ClickHouseResult ClickHouseClient::listColumns() const {
    return query(
        "SELECT table, name, type "
        "FROM system.columns "
        "WHERE database = currentDatabase() "
        "ORDER BY table, position "
        "FORMAT TabSeparatedWithNamesAndTypes"
    );
}

BacktestCommissionsRepository::BacktestCommissionsRepository(
    ClickHouseClient client,
    std::string tableName
) : client_(std::move(client)),
    quotedTableName_(quoteQualifiedIdentifier(tableName)) {
}

ClickHouseResult BacktestCommissionsRepository::fetch(std::size_t limit) const {
    if (limit == 0) {
        throw std::invalid_argument("The commissions row limit must be greater than zero");
    }

    const std::string sql = "SELECT * FROM " + quotedTableName_ +
        " LIMIT " + std::to_string(limit) +
        " FORMAT TabSeparatedWithNamesAndTypes";
    return client_.query(sql);
}

EndOfOrderStream::EndOfOrderStream()
    : std::out_of_range("No more orders in the backtest stream") {
}

Order orderFromCommissionRow(
    const ClickHouseResult& result,
    std::size_t rowIndex
) {
    const Order::OrderId id = parseNumber<Order::OrderId>(
        requiredValue(result, rowIndex, "id"),
        "id"
    );
    const Order::Quantity quantity = parseNumber<Order::Quantity>(
        requiredValue(result, rowIndex, "commission_amount"),
        "commission_amount"
    );
    const Order::Price price = parseNumber<Order::Price>(
        requiredValue(result, rowIndex, "commission_price"),
        "commission_price"
    );

    const std::string& type = requiredValue(
        result,
        rowIndex,
        "commission_type"
    );
    OrderAction action{};
    if (type == "B") {
        action = OrderAction::BUY;
    } else if (type == "S") {
        action = OrderAction::SELL;
    } else if (type == "R" || type == "C") {
        action = OrderAction::RECALL;
    } else {
        throw std::runtime_error("Unknown commission_type: " + type);
    }

    return Order(id, action, quantity, price);
}

BacktestOrderStream::BacktestOrderStream(
    ClickHouseClient client,
    std::string tableName,
    std::size_t pageSize
) : client_(std::move(client)),
    quotedTableName_(quoteQualifiedIdentifier(tableName)),
    pageSize_(pageSize) {
    if (pageSize_ == 0) {
        throw std::invalid_argument("The order stream page size must be greater than zero");
    }
}

Order BacktestOrderStream::nextOrder() {
    if (nextRow_ >= page_.rows.size()) {
        fetchNextPage();
    }
    if (exhausted_) {
        throw EndOfOrderStream();
    }

    const std::size_t rowIndex = nextRow_++;
    cursorDatetime_ = requiredValue(page_, rowIndex, "datetime");
    cursorId_ = parseNumber<Order::OrderId>(
        requiredValue(page_, rowIndex, "id"),
        "id"
    );
    return orderFromCommissionRow(page_, rowIndex);
}

bool BacktestOrderStream::exhausted() const noexcept {
    return exhausted_;
}

const std::optional<std::string>& BacktestOrderStream::lastDatetime() const noexcept {
    return cursorDatetime_;
}

void BacktestOrderStream::fetchNextPage() {
    if (exhausted_) {
        return;
    }

    std::string sql =
        "SELECT datetime, commission_price, commission_amount, "
        "commission_type, id "
        "FROM " + quotedTableName_ + " "
        "WHERE data_batch = 'commission'";

    if (cursorDatetime_.has_value() && cursorId_.has_value()) {
        sql += " AND (datetime, id) > (toDateTime64(" +
            quoteSqlString(*cursorDatetime_) + ", 3), " +
            std::to_string(*cursorId_) + ")";
    }

    sql += " ORDER BY datetime, id LIMIT " + std::to_string(pageSize_) +
        " FORMAT TabSeparatedWithNamesAndTypes";

    page_ = client_.query(sql);
    nextRow_ = 0;
    exhausted_ = page_.rows.empty();
}
