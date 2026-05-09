cmake_minimum_required(VERSION 3.16)

project(nfl3 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/tests/include)

add_executable(nfl3
    src/main.cpp
    src/util/CsvParser.cpp
    src/model/Team.cpp
    src/model/Game.cpp
    src/model/Season.cpp
    src/model/Tiebreaker.cpp
    src/output/AsciiPrinter.cpp
)

# Test executable
enable_testing()

add_executable(nfl3_tests
    tests/test_main.cpp
    tests/test_team.cpp
    tests/test_game.cpp
    tests/test_tiebreaker.cpp
    tests/test_season.cpp
    src/util/CsvParser.cpp
    src/model/Team.cpp
    src/model/Game.cpp
    src/model/Season.cpp
    src/model/Tiebreaker.cpp
    src/output/AsciiPrinter.cpp
)

add_test(NAME nfl3_tests COMMAND nfl3_tests)
