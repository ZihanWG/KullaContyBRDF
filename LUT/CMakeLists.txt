cmake_minimum_required(VERSION 3.16)
project(KullaContyLUTGenerator LANGUAGES CXX)

add_executable(KullaContyLUTGenerator LUT/LUT.cpp)
target_compile_features(KullaContyLUTGenerator PRIVATE cxx_std_17)
target_include_directories(KullaContyLUTGenerator PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(KullaContyImageCompare Compare/Compare.cpp)
target_compile_features(KullaContyImageCompare PRIVATE cxx_std_17)
target_include_directories(KullaContyImageCompare PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(KullaContyLUTGenerator PRIVATE /W4 /permissive-)
    target_compile_options(KullaContyImageCompare PRIVATE /W4 /permissive-)
else()
    target_compile_options(KullaContyLUTGenerator PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(KullaContyImageCompare PRIVATE -Wall -Wextra -Wpedantic)
endif()
