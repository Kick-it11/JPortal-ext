cmake_minimum_required(VERSION 3.10)
project(grapbc)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -pthread")

add_executable( grapbc
                src/grapbc.cpp
                src/classFileParser.cpp
                src/constantPool.cpp
                src/method.cpp
                src/bytecodes.cpp)
