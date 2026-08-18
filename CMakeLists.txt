cmake_minimum_required(VERSION 3.25)

project(OrderBook VERSION 0.1.0 LANGUAGES CXX)

find_package(CURL REQUIRED)

function(orderbook_set_defaults target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
        )
    endif()
endfunction()

add_library(orderbook_core
    Order.cpp
    OrderBook.cpp
    PriceLevel.cpp
    MatchingEngine.cpp
    Trades.cpp
    TradingSession.cpp
    Strategy.cpp
)
target_include_directories(orderbook_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
orderbook_set_defaults(orderbook_core)

add_library(clickhouse_access ClickHouseClient.cpp)
target_include_directories(clickhouse_access PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(clickhouse_access
    PUBLIC orderbook_core
    PRIVATE CURL::libcurl
)
orderbook_set_defaults(clickhouse_access)

add_executable(orderbook
    main.cpp
    PriceGui.cpp
)
target_link_libraries(orderbook PRIVATE clickhouse_access)
orderbook_set_defaults(orderbook)

add_executable(backtest_commissions BacktestCommissionsMain.cpp)
target_link_libraries(backtest_commissions PRIVATE clickhouse_access)
orderbook_set_defaults(backtest_commissions)

include(CTest)
if(BUILD_TESTING)
    add_executable(orderbook_core_tests tests/OrderBookTest.cpp)
    target_link_libraries(orderbook_core_tests PRIVATE orderbook_core)
    orderbook_set_defaults(orderbook_core_tests)
    add_test(NAME orderbook_core_tests COMMAND orderbook_core_tests)

    add_executable(clickhouse_access_tests tests/ClickHouseClientTest.cpp)
    target_link_libraries(clickhouse_access_tests PRIVATE clickhouse_access)
    orderbook_set_defaults(clickhouse_access_tests)
    add_test(NAME clickhouse_access_tests COMMAND clickhouse_access_tests)
endif()
