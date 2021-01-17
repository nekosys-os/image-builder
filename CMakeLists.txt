cmake_minimum_required(VERSION 3.17)
project(image_builder)

set(CMAKE_CXX_STANDARD 17)

add_executable(image_builder main.cpp buffer.cpp buffer.h file_content.h)