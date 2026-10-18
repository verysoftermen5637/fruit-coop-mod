cmake_minimum_required(VERSION 3.18)
project(fruit_coop_mod CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(coop_fp SHARED
    coop_mod.cpp
)

target_link_libraries(coop_fp
    PRIVATE
    log
)
