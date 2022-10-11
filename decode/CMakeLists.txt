cmake_minimum_required(VERSION 2.8)
project(decode)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -pthread -std=c++11")

include_directories(include)

aux_source_directory(src/decoder/ decoder_srcs)
aux_source_directory(src/java/ java_srcs)
aux_source_directory(src/runtime/ runtime_srcs)
aux_source_directory(src/output/ output_srcs)
aux_source_directory(src/pt/ pt_srcs)
aux_source_directory(src/task/ task_srcs)
aux_source_directory(src/utilities/ utilities_srcs)

add_executable(decode src/main.cpp
  ${decoder_srcs}
  ${java_srcs}
  ${runtime_srcs}
  ${output_srcs}
  ${pt_srcs}
  ${task_srcs}
  ${utilities_srcs}
)
