cmake_minimum_required(VERSION 3.16)
project(trading LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(Boost QUIET)

add_executable(benchmark
    src/benchmark.cpp
    src/matching_engine.cpp
    src/pnl_thread.cpp
)

target_link_libraries(benchmark PRIVATE Threads::Threads)
target_include_directories(benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (MSVC)
    target_compile_options(benchmark PRIVATE /W4 $<$<CONFIG:Release>:/O2>)
else()
    target_compile_options(benchmark PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Release>:-O3>)
endif()

if (Boost_FOUND)
    add_executable(trading
        src/main.cpp
        src/matching_engine.cpp
        src/pnl_thread.cpp
        src/tcp_server.cpp
        src/client_session.cpp
        src/protocol.cpp
        src/message.cpp
    )
    target_link_libraries(trading PRIVATE Threads::Threads Boost::headers)
    target_include_directories(trading PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${Boost_INCLUDE_DIRS})
    if (MSVC)
        target_compile_options(trading PRIVATE /W4 $<$<CONFIG:Release>:/O2>)
    else()
        target_compile_options(trading PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Release>:-O3>)
    endif()
endif()
