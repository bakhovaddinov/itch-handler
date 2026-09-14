cmake_minimum_required(VERSION 3.15)
project(ITCH_Feed_Handler VERSION 1.0 LANGUAGES CXX)

# Enforce modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define the main executable
add_executable(feed_handler src/main.cpp)

# HFT-specific performance flags and strict warnings
target_compile_options(feed_handler PRIVATE 
    -Wall -Wextra -Wpedantic -Werror 
    -O3 -march=native
)

# Allow #include "itch_messages.hpp" directly in your src files
target_include_directories(feed_handler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)