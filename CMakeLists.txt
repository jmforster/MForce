cmake_minimum_required(VERSION 3.20)
project(MForce LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(engine)
add_subdirectory(tools/mforce_cli)
add_subdirectory(tools/mforce_keys)
