cmake_minimum_required(VERSION 3.16)

project(nfl3 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# OpenMP for parallel Monte Carlo simulation
find_package(OpenMP)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/tests/include)

add_executable(nfl3
    src/main.cpp
    src/app/CommandSupport.cpp
    src/util/CsvParser.cpp
    src/util/BacktestFixtureLoader.cpp
    src/model/Team.cpp
    src/model/Game.cpp
    src/model/Season.cpp
    src/model/Tiebreaker.cpp
    src/model/MonteCarlo.cpp
    src/output/AsciiPrinter.cpp
    src/output/WebServer.cpp
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(nfl3 OpenMP::OpenMP_CXX)
endif()

# Test executable
enable_testing()

add_executable(nfl3_tests
    tests/test_main.cpp
    tests/test_backtest_fixture_loader.cpp
    tests/test_cli_integration.cpp
    tests/test_commands_web.cpp
    tests/test_team.cpp
    tests/test_game.cpp
    tests/test_tiebreaker.cpp
    tests/test_season.cpp
    tests/test_historical_replay.cpp
    tests/test_montecarlo.cpp
    src/util/CsvParser.cpp
    src/util/BacktestFixtureLoader.cpp
    src/app/CommandSupport.cpp
    src/model/Team.cpp
    src/model/Game.cpp
    src/model/Season.cpp
    src/model/Tiebreaker.cpp
    src/model/MonteCarlo.cpp
    src/output/AsciiPrinter.cpp
    src/output/WebServer.cpp
)

target_compile_definitions(nfl3_tests PRIVATE NFL3_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(OpenMP_CXX_FOUND)
    target_link_libraries(nfl3_tests OpenMP::OpenMP_CXX)
endif()

add_test(NAME nfl3_tests COMMAND nfl3_tests)
