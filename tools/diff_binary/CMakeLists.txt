cmake_minimum_required(VERSION 3.10)
project(diff_binary)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -pthread")

add_executable(diff_binary diff_binary.cpp)
