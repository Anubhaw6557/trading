cmake_minimum_required(VERSION 3.16)
project(trading LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(Boost_NO_BOOST_CMAKE ON)

find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

add_executable(trading
    src/main.cpp
    src/matching_engine.cpp
    src/pnl_thread.cpp
    src/tcp_server.cpp
    src/client_session.cpp
    src/protocol.cpp
    src/message.cpp

)

target_link_libraries(trading
    PRIVATE
    Threads::Threads
)

target_include_directories(trading PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(trading PRIVATE /W4)
else()
    target_compile_options(trading PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (MSVC)
    target_compile_options(trading PRIVATE
        $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(trading PRIVATE
        $<$<CONFIG:Release>:-O3>
    )
endif()



