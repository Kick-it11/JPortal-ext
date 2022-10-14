cmake_minimum_required(VERSION 2.8)
project(decode)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -pthread -std=c++11")

include_directories(include)

aux_source_directory(src/decoder/ decoder_srcs)
aux_source_directory(src/insn/ insn_srcs)
aux_source_directory(src/java/ java_srcs)
aux_source_directory(src/runtime/ runtime_srcs)
aux_source_directory(src/pt/ pt_srcs)
aux_source_directory(src/thread/ thread_srcs)
aux_source_directory(src/utilities/ utilities_srcs)
aux_source_directory(src/sideband/ sideband_srcs)

add_executable(decode src/decode.cpp
  ${decoder_srcs}
  ${insn_srcs}
  ${java_srcs}
  ${runtime_srcs}
  ${pt_srcs}
  ${thread_srcs}
  ${utilities_srcs}
  ${sideband_srcs}
)
