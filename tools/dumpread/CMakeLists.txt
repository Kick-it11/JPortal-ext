cmake_minimum_required(VERSION 3.10)
project(dumpread)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -pthread")

add_executable(dumpread dumpread.cpp pcDesc.cpp scopeDesc.cpp)