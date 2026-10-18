cmake_minimum_required(VERSION 3.18)
project(fruit_coop_mod CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Dobby Hook paths
set(DOBBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dobby)
include_directories(${DOBBY_DIR}/include)

# Imported static library for libdobby.a
add_library(dobby STATIC IMPORTED)
set_target_properties(dobby PROPERTIES
    IMPORTED_LOCATION ${DOBBY_DIR}/lib/libdobby.a
)

# Shared library for Android injection
add_library(coop_fp SHARED
    coop_mod.cpp
)

target_link_libraries(coop_fp
    PRIVATE
    dobby
    log
)
