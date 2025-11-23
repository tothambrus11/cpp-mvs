cmake_minimum_required(VERSION 3.28)
project(cpp_mvs)

set(CMAKE_CXX_STANDARD 23)

# Setup CPM
file(DOWNLOAD
     https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
     ${CMAKE_BINARY_DIR}/cmake/CPM.cmake
     EXPECTED_HASH SHA256=c8cdc32c03816538ce22781ed72964dc864b2a34a310d3b7104812a5ca2d835d
)
include(${CMAKE_BINARY_DIR}/cmake/CPM.cmake)

# Add doctest
CPMAddPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    VERSION 2.4.12
)

add_library(cpp_mvs STATIC library.cpp)

add_executable(unit_tests tests.cpp)
target_link_libraries(unit_tests PRIVATE cpp_mvs doctest::doctest)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
