# Copyright 2023 Nokia
# Copyright 2026 Shi-Zhong Chen
# Licensed under the BSD 3 Clause license
# SPDX-License-Identifier: BSD-3-Clause

message("\n*** cmake lib_domino: so users can call this CMakeLists.txt in their own project ***")

file(GLOB_RECURSE  list_src  CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
add_library(lib_domino  ${list_src})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(lib_domino  PRIVATE  -Wall -Wextra -Wswitch-enum -Werror)
endif()

target_include_directories(lib_domino  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/domino
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/msg_self
    ${CMAKE_CURRENT_SOURCE_DIR}/obj_anywhere
    ${CMAKE_CURRENT_SOURCE_DIR}/safe_mem
    ${CMAKE_CURRENT_SOURCE_DIR}/thread
)

find_package(Threads REQUIRED)
target_link_libraries(lib_domino  PUBLIC  Threads::Threads)
