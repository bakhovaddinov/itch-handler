cmake_minimum_required(VERSION 3.15)
project(ITCH_Feed_Handler VERSION 1.0 LANGUAGES CXX)

# Enforce modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define the main executable
add_executable(feed_handler src/main.cpp)

# Strict warnings (always on)
target_compile_options(feed_handler PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

# HFT perf flags: only for optimized Release builds
target_compile_options(feed_handler PRIVATE
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
)

# Allow #include "itch_messages.hpp" directly in your src files
target_include_directories(feed_handler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Optional sanitizer build.
# Sanitizers MUST be applied to BOTH compile AND link, otherwise you get
# undefined references to __asan_init / __ubsan_handle_* at link time.
option(ENABLE_SANITIZERS "Build with Address + UndefinedBehavior sanitizers" OFF)
if(ENABLE_SANITIZERS)
    target_compile_options(feed_handler PRIVATE
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -g
    )
    target_link_options(feed_handler PRIVATE
        -fsanitize=address,undefined
    )
endif()