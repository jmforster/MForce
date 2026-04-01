cmake_minimum_required(VERSION 3.20)
project(MForce LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GLFW options
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(engine/third_party/glfw)

add_subdirectory(engine)
add_subdirectory(tools/mforce_cli)
add_subdirectory(tools/mforce_keys)
add_subdirectory(tools/mforce_ui)
