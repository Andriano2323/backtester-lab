cmake_minimum_required(VERSION 3.16)

project(market_data_ingestion LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

option(ENABLE_COVERAGE "Build with gcov coverage instrumentation" OFF)
option(ENABLE_ARROW "Build optional Arrow/Feather input support" OFF)
option(BACKTESTER_BUILD_PYTHON "Build optional pybind11 Python extension" OFF)

set(INGEST_SOURCES
    src/app/ArgsParser.cpp
    src/backtest/IntegratedBacktestEngine.cpp
    src/backtest/MarketDataEventAdapter.cpp
    src/backtest/OrderExecutionBridge.cpp
    src/book/BookManager.cpp
    src/book/LimitOrderBook.cpp
    src/domain/MarketDataEvent.cpp
    src/gateway/OrderGatewayClient.cpp
    src/gateway/OrderGatewayServer.cpp
    src/lob/EngineView.cpp
    src/lob/FillSimulator.cpp
    src/lob/HistoricalLOB.cpp
    src/lob/HistoricalLobProcessor.cpp
    src/lob/HistoricalLobStore.cpp
    src/lob/SimulatedLOB.cpp
    src/messaging/MarketDataGapDetector.cpp
    src/messaging/MarketDataPublisher.cpp
    src/parsing/JsonParser.cpp
    src/processing/AsyncSnapshotWriter.cpp
    src/processing/LoggingMarketDataEventProcessor.cpp
    src/processing/LobMarketDataEventProcessor.cpp
    src/processing/ShardedLobMarketDataEventProcessor.cpp
    src/runners/BenchmarkRunner.cpp
    src/runners/FlatMergeRunner.cpp
    src/runners/HardRunnerSupport.cpp
    src/runners/HierarchicalMergeRunner.cpp
    src/runners/InputFileDiscovery.cpp
    src/runners/ResultPrinter.cpp
    src/runners/StandardRunner.cpp
    third_party/simdjson/simdjson.cpp
)

set(INGEST_TEST_SOURCES
    tests/test_main.cpp
    tests/app/ArgsParserTest.cpp
    tests/backtest/IntegratedBacktestEngineTest.cpp
    tests/backtest/MarketDataEventAdapterTest.cpp
    tests/backtest/OrderExecutionBridgeTest.cpp
    tests/book/BookManagerTest.cpp
    tests/book/LimitOrderBookTest.cpp
    tests/concurrency/NonBlockingQueueTest.cpp
    tests/domain/MarketDataEventTest.cpp
    tests/lob/EngineViewTest.cpp
    tests/lob/FillSimulatorTest.cpp
    tests/lob/HistoricalLOBTest.cpp
    tests/lob/HistoricalLobProcessorTest.cpp
    tests/lob/LobSummaryIntegrationTest.cpp
    tests/lob/SimulatedLOBTest.cpp
    tests/lob/ThreadSafetyTest.cpp
    tests/parsing/JsonParserTest.cpp
    tests/processing/AsyncSnapshotWriterTest.cpp
    tests/processing/LobMarketDataEventProcessorTest.cpp
    tests/runners/FlatMergeRunnerTest.cpp
    tests/runners/HardLobIntegrationTest.cpp
    tests/runners/HierarchicalMergeRunnerTest.cpp
    tests/runners/ResultPrinterTest.cpp
    tests/runners/StandardRunnerLobIntegrationTest.cpp
    tests/runners/StandardRunnerTest.cpp
)

if (ENABLE_ARROW)
    find_package(Arrow CONFIG QUIET)
    if (Arrow_FOUND)
        message(STATUS "Arrow C++ found via CMake package")
        set(ARROW_TARGET Arrow::arrow_shared)
    else()
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c "import pathlib, pyarrow; print(pathlib.Path(pyarrow.__file__).parent)"
            OUTPUT_VARIABLE PYARROW_ROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE PYARROW_RESULT
        )
        if (NOT PYARROW_RESULT EQUAL 0)
            message(FATAL_ERROR "ENABLE_ARROW=ON requires Arrow C++ or a pyarrow wheel with bundled headers/libs")
        endif()

        set(PYARROW_INCLUDE_DIR "${PYARROW_ROOT}/include")
        file(GLOB PYARROW_ARROW_LIBS "${PYARROW_ROOT}/libarrow.so*")
        list(SORT PYARROW_ARROW_LIBS)
        if (NOT PYARROW_ARROW_LIBS)
            message(FATAL_ERROR "Could not find libarrow.so under pyarrow at ${PYARROW_ROOT}")
        endif()
        list(GET PYARROW_ARROW_LIBS 0 PYARROW_ARROW_LIB)
        if (NOT EXISTS "${PYARROW_INCLUDE_DIR}/arrow/api.h" OR NOT EXISTS "${PYARROW_ARROW_LIB}")
            message(FATAL_ERROR "Could not find Arrow headers/libs under pyarrow at ${PYARROW_ROOT}")
        endif()

        add_library(Arrow::arrow_shared SHARED IMPORTED)
        set_target_properties(Arrow::arrow_shared PROPERTIES
            IMPORTED_LOCATION "${PYARROW_ARROW_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${PYARROW_INCLUDE_DIR}"
        )
        set(ARROW_TARGET Arrow::arrow_shared)
        set(ARROW_RUNTIME_DIR "${PYARROW_ROOT}")
        message(STATUS "Arrow C++ found via pyarrow at ${PYARROW_ROOT}")
    endif()

    list(APPEND INGEST_SOURCES
        src/io/FeatherEventReader.cpp
        src/runners/FeatherHardRunnerSupport.cpp
    )
    list(APPEND INGEST_TEST_SOURCES
        tests/io/FeatherEventReaderTest.cpp
    )
endif()

add_library(ingest_core STATIC ${INGEST_SOURCES})
target_include_directories(ingest_core PUBLIC src)
target_include_directories(ingest_core PRIVATE third_party/simdjson)
target_link_libraries(ingest_core PUBLIC Threads::Threads)
if (ENABLE_ARROW)
    target_compile_definitions(ingest_core PUBLIC MD_ENABLE_ARROW=1)
    target_link_libraries(ingest_core PUBLIC ${ARROW_TARGET})
endif()

if (BACKTESTER_BUILD_PYTHON)
    if (NOT Python3_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python")
        set(Python3_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python" CACHE FILEPATH "Python interpreter")
    endif()

    find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
    find_package(pybind11 CONFIG QUIET)
    if (NOT pybind11_FOUND)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c "import pybind11; print(pybind11.get_cmake_dir())"
            OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE PYBIND11_CMAKE_DIR_RESULT
        )
        if (NOT PYBIND11_CMAKE_DIR_RESULT EQUAL 0 OR NOT EXISTS "${PYBIND11_CMAKE_DIR}")
            message(FATAL_ERROR "BACKTESTER_BUILD_PYTHON=ON requires pybind11. Install it in the selected Python environment or set pybind11_DIR.")
        endif()

        set(pybind11_DIR "${PYBIND11_CMAKE_DIR}" CACHE PATH "pybind11 CMake package directory" FORCE)
        find_package(pybind11 CONFIG REQUIRED)
    endif()

    set_target_properties(ingest_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

    pybind11_add_module(_backtester_cpp MODULE bindings/python/backtester_module.cpp)
    target_link_libraries(_backtester_cpp PRIVATE ingest_core)
    target_compile_definitions(_backtester_cpp PRIVATE BACKTESTER_VERSION="0.0.0")
    set_target_properties(_backtester_cpp PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/backtester"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/backtester"
    )
    add_custom_command(TARGET _backtester_cpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/python"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:_backtester_cpp>"
            "${CMAKE_BINARY_DIR}/python/$<TARGET_FILE_NAME:_backtester_cpp>"
    )
endif()

add_executable(ingest src/main.cpp)
target_link_libraries(ingest PRIVATE ingest_core)
if (ENABLE_ARROW AND DEFINED ARROW_RUNTIME_DIR)
    set_target_properties(ingest PROPERTIES BUILD_RPATH "${ARROW_RUNTIME_DIR}")
endif()

add_executable(market_data_feed_benchmark benchmarks/market_data_feed_benchmark.cpp)
target_link_libraries(market_data_feed_benchmark PRIVATE ingest_core)
if (ENABLE_ARROW AND DEFINED ARROW_RUNTIME_DIR)
    set_target_properties(market_data_feed_benchmark PROPERTIES BUILD_RPATH "${ARROW_RUNTIME_DIR}")
endif()

add_executable(order_gateway_latency_benchmark benchmarks/order_gateway_latency_benchmark.cpp)
target_link_libraries(order_gateway_latency_benchmark PRIVATE ingest_core)
if (ENABLE_ARROW AND DEFINED ARROW_RUNTIME_DIR)
    set_target_properties(order_gateway_latency_benchmark PROPERTIES BUILD_RPATH "${ARROW_RUNTIME_DIR}")
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ingest_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ingest PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(market_data_feed_benchmark PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(order_gateway_latency_benchmark PRIVATE -Wall -Wextra -Wpedantic)
    if (BACKTESTER_BUILD_PYTHON)
        target_compile_options(_backtester_cpp PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    set_source_files_properties(third_party/simdjson/simdjson.cpp PROPERTIES
        COMPILE_FLAGS "-Wno-error -w")

    if (ENABLE_COVERAGE)
        target_compile_options(ingest_core PUBLIC -O0 -g --coverage)
        target_compile_options(ingest PRIVATE -O0 -g --coverage)
        target_compile_options(market_data_feed_benchmark PRIVATE -O0 -g --coverage)
        target_compile_options(order_gateway_latency_benchmark PRIVATE -O0 -g --coverage)
        if (BACKTESTER_BUILD_PYTHON)
            target_compile_options(_backtester_cpp PRIVATE -O0 -g --coverage)
        endif()
        target_link_options(ingest_core PUBLIC --coverage)
        target_link_options(ingest PRIVATE --coverage)
        target_link_options(market_data_feed_benchmark PRIVATE --coverage)
        target_link_options(order_gateway_latency_benchmark PRIVATE --coverage)
        if (BACKTESTER_BUILD_PYTHON)
            target_link_options(_backtester_cpp PRIVATE --coverage)
        endif()
    endif()
endif()

enable_testing()
add_executable(ingest_tests ${INGEST_TEST_SOURCES})
target_link_libraries(ingest_tests PRIVATE ingest_core)
target_include_directories(ingest_tests PRIVATE tests)
target_compile_definitions(ingest_tests PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/test_data")
if (ENABLE_ARROW AND DEFINED ARROW_RUNTIME_DIR)
    set_target_properties(ingest_tests PROPERTIES BUILD_RPATH "${ARROW_RUNTIME_DIR}")
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ingest_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(ingest_tests PRIVATE -O0 -g --coverage)
        target_link_options(ingest_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME ingest_tests COMMAND ingest_tests)

add_executable(domain_contract_tests tests/test_domain_types.cpp)
target_link_libraries(domain_contract_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(domain_contract_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(domain_contract_tests PRIVATE -O0 -g --coverage)
        target_link_options(domain_contract_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME domain_contract_tests COMMAND domain_contract_tests)

add_executable(non_blocking_queue_tests tests/test_non_blocking_queue.cpp)
target_link_libraries(non_blocking_queue_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(non_blocking_queue_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(non_blocking_queue_tests PRIVATE -O0 -g --coverage)
        target_link_options(non_blocking_queue_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME non_blocking_queue_tests COMMAND non_blocking_queue_tests)

add_executable(market_data_message_tests tests/test_market_data_messages.cpp)
target_link_libraries(market_data_message_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(market_data_message_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(market_data_message_tests PRIVATE -O0 -g --coverage)
        target_link_options(market_data_message_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME market_data_message_tests COMMAND market_data_message_tests)

add_executable(market_data_subscriber_tests tests/test_market_data_subscriber.cpp)
target_link_libraries(market_data_subscriber_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(market_data_subscriber_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(market_data_subscriber_tests PRIVATE -O0 -g --coverage)
        target_link_options(market_data_subscriber_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME market_data_subscriber_tests COMMAND market_data_subscriber_tests)

add_executable(market_data_publisher_tests tests/test_market_data_publisher.cpp)
target_link_libraries(market_data_publisher_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(market_data_publisher_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(market_data_publisher_tests PRIVATE -O0 -g --coverage)
        target_link_options(market_data_publisher_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME market_data_publisher_tests COMMAND market_data_publisher_tests)

add_executable(market_data_gap_detector_tests tests/test_market_data_gap_detector.cpp)
target_link_libraries(market_data_gap_detector_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(market_data_gap_detector_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(market_data_gap_detector_tests PRIVATE -O0 -g --coverage)
        target_link_options(market_data_gap_detector_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME market_data_gap_detector_tests COMMAND market_data_gap_detector_tests)

add_executable(order_message_tests tests/test_order_messages.cpp)
target_link_libraries(order_message_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(order_message_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(order_message_tests PRIVATE -O0 -g --coverage)
        target_link_options(order_message_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME order_message_tests COMMAND order_message_tests)

add_executable(order_channel_tests tests/test_order_channel.cpp)
target_link_libraries(order_channel_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(order_channel_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(order_channel_tests PRIVATE -O0 -g --coverage)
        target_link_options(order_channel_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME order_channel_tests COMMAND order_channel_tests)

add_executable(order_gateway_client_tests tests/test_order_gateway_client.cpp)
target_link_libraries(order_gateway_client_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(order_gateway_client_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(order_gateway_client_tests PRIVATE -O0 -g --coverage)
        target_link_options(order_gateway_client_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME order_gateway_client_tests COMMAND order_gateway_client_tests)

add_executable(order_gateway_server_session_tests tests/test_order_gateway_server_sessions.cpp)
target_link_libraries(order_gateway_server_session_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(order_gateway_server_session_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(order_gateway_server_session_tests PRIVATE -O0 -g --coverage)
        target_link_options(order_gateway_server_session_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME order_gateway_server_session_tests COMMAND order_gateway_server_session_tests)

add_executable(order_gateway_server_validation_tests tests/test_order_gateway_server_validation.cpp)
target_link_libraries(order_gateway_server_validation_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(order_gateway_server_validation_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(order_gateway_server_validation_tests PRIVATE -O0 -g --coverage)
        target_link_options(order_gateway_server_validation_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME order_gateway_server_validation_tests COMMAND order_gateway_server_validation_tests)

add_executable(order_gateway_fill_tests tests/test_order_gateway_fills.cpp)
target_link_libraries(order_gateway_fill_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(order_gateway_fill_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(order_gateway_fill_tests PRIVATE -O0 -g --coverage)
        target_link_options(order_gateway_fill_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME order_gateway_fill_tests COMMAND order_gateway_fill_tests)

add_executable(order_gateway_end_to_end_tests tests/test_order_gateway_end_to_end.cpp)
target_link_libraries(order_gateway_end_to_end_tests PRIVATE ingest_core)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(order_gateway_end_to_end_tests PRIVATE -Wall -Wextra -Wpedantic)
    if (ENABLE_COVERAGE)
        target_compile_options(order_gateway_end_to_end_tests PRIVATE -O0 -g --coverage)
        target_link_options(order_gateway_end_to_end_tests PRIVATE --coverage)
    endif()
endif()
add_test(NAME order_gateway_end_to_end_tests COMMAND order_gateway_end_to_end_tests)

add_test(
    NAME market_data_feed_benchmark_smoke
    COMMAND market_data_feed_benchmark --subscribers 2 --messages 1000 --batch-size 64
)
set_tests_properties(market_data_feed_benchmark_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "subscribers,messages,total_deliveries,wall_clock_seconds,publishes_per_second,deliveries_per_second[\r\n]+2,1000,2000,"
)

add_test(
    NAME order_gateway_latency_benchmark_smoke
    COMMAND order_gateway_latency_benchmark --orders 1000 --batch-size 64 --threaded 1
)
set_tests_properties(order_gateway_latency_benchmark_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "orders,batch_size,threaded,total_acks,wall_clock_seconds,orders_per_second,avg_latency_ns,min_latency_ns,p50_latency_ns,p95_latency_ns,p99_latency_ns,max_latency_ns[\r\n]+1000,64,1,1000,"
)

if (BACKTESTER_BUILD_PYTHON)
    add_test(
        NAME python_binding_import_tests
        COMMAND ${Python3_EXECUTABLE} -c "import backtester; import _backtester_cpp; assert _backtester_cpp.version(); assert _backtester_cpp.PRICE_SCALE == 1000000000; assert _backtester_cpp.UNDEFINED_PRICE == 9223372036854775807"
    )
    set_tests_properties(python_binding_import_tests PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python:${CMAKE_BINARY_DIR}/python"
    )
endif()

add_test(
    NAME ingest_smoke_standard
    COMMAND ingest --mode standard --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_data/single_valid.ndjson --print-events 0
)
set_tests_properties(ingest_smoke_standard PROPERTIES
    PASS_REGULAR_EXPRESSION "Summary[\r\n]+total_messages_processed=2[\r\n]+chronological_violations=0[\r\n]+first_timestamp=[0-9]+[\r\n]+last_timestamp=[0-9]+[\r\n]+wall_clock_seconds=[0-9]+\\.[0-9]+[\r\n]+throughput_messages_per_second=[0-9]+\\.[0-9]+"
)

add_test(
    NAME ingest_smoke_flat
    COMMAND ingest --mode flat --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_data/multi --print-events 0
)
set_tests_properties(ingest_smoke_flat PROPERTIES
    PASS_REGULAR_EXPRESSION "chronological_violations=0"
)

add_test(
    NAME ingest_smoke_hierarchy
    COMMAND ingest --mode hierarchy --input ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_data/multi --print-events 0
)
set_tests_properties(ingest_smoke_hierarchy PROPERTIES
    PASS_REGULAR_EXPRESSION "chronological_violations=0"
)

add_test(NAME ingest_smoke_help COMMAND ingest --help)
set_tests_properties(ingest_smoke_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")
