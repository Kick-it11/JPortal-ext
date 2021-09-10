cmake_minimum_required(VERSION 3.10)
project(ptread)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -pthread")

include_directories(include)

add_executable(ptread src/main.cpp src/load_file.cpp src/pevent.cpp src/ptjvm_decoder.cpp)
target_link_libraries(ptread
  ipt
)