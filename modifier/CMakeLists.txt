cmake_minimum_required(VERSION 3.10)
project(modifier)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -pthread")

add_executable(modifier
                src/modifier.cpp
                src/constantPool.cpp)