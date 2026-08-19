cmake_minimum_required(VERSION 3.16)
project(KullaContyLUTGenerator LANGUAGES CXX)

add_executable(KullaContyLUTGenerator LUT/LUT.cpp)
target_compile_features(KullaContyLUTGenerator PRIVATE cxx_std_17)
target_include_directories(KullaContyLUTGenerator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(KullaContyLUTGenerator PRIVATE /W4 /permissive-)
else()
    target_compile_options(KullaContyLUTGenerator PRIVATE -Wall -Wextra -Wpedantic)
endif()
