cmake_minimum_required(VERSION 3.20)

project(NetworkChat)

set(CMAKE_CXX_STANDARD 17)

add_executable(
    server
    server/src/main.cpp
 "common.h")

add_executable(
    client
    client/src/main.cpp
 "common.h")