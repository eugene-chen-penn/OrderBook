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
    LOB/Order.cpp
    LOB/OrderBook.cpp
    LOB/PriceLevel.cpp
    LOB/MatchingEngine.cpp
    LOB/Trades.cpp
    LOB/Strategy.cpp
)
target_include_directories(orderbook_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/LOB)
orderbook_set_defaults(orderbook_core)

add_library(clickhouse_access LOB/ClickHouseClient.cpp)
target_include_directories(clickhouse_access PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/LOB)
target_link_libraries(clickhouse_access
    PUBLIC orderbook_core
    PRIVATE CURL::libcurl
)
orderbook_set_defaults(clickhouse_access)

add_executable(orderbook
    main.cpp
    LOB/PriceGui.cpp
)
target_link_libraries(orderbook PRIVATE clickhouse_access)
orderbook_set_defaults(orderbook)

add_executable(backtest_commissions LOB/BacktestCommissionsMain.cpp)
target_link_libraries(backtest_commissions PRIVATE clickhouse_access)
orderbook_set_defaults(backtest_commissions)

add_library(mdhub
    MdHub/LinearBoard.cpp
    MdHub/SpscRingQueue.cpp
)
target_include_directories(mdhub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/MdHub)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(mdhub PUBLIC rt)
endif()
orderbook_set_defaults(mdhub)

add_executable(mdhub_test MdHub/test.cpp)
target_link_libraries(mdhub_test PRIVATE mdhub)
orderbook_set_defaults(mdhub_test)

add_executable(linear_board_test MdHub/test.cpp)
target_compile_definitions(linear_board_test PRIVATE MDHUB_TEST_LINEAR_BOARD=1)
target_link_libraries(linear_board_test PRIVATE mdhub)
orderbook_set_defaults(linear_board_test)

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

    add_test(NAME mdhub_test COMMAND mdhub_test)
    add_test(NAME linear_board_test COMMAND linear_board_test)
endif()
