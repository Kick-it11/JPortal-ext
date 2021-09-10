cmake_minimum_required(VERSION 3.10)
project(sbread)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -pthread")

add_executable(sbread sb_decoder.cpp pevent.cpp)