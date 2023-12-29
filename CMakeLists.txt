# Minimum necessary version for supporting CMAKE_OSX_ARCHITECTURES
cmake_minimum_required(VERSION 3.19)

project(dr-single-file-cexts)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)
endif()

# Debugging information (do we need this always?)
add_compile_options(-g)

# Optimizations
add_compile_options(-O2)

# Position independent code since we want to build a shared library
add_compile_options(-fpic)

include_directories(include)

add_library(minheap SHARED minheap.c)
