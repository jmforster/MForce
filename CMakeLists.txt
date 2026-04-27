cmake_minimum_required(VERSION 3.20)
project(MForce LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Emit PDBs in Release so the crash-log SEH filter can resolve symbols.
# /Zi adds debug info to .obj; /DEBUG tells the linker to write a .pdb;
# /OPT:REF and /OPT:ICF restore Release optimizations that /DEBUG disables.
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    add_link_options(
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
endif()

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
add_subdirectory(tools/durn_converter)
add_subdirectory(tools/test_figures)
add_subdirectory(tools/ppl_to_json)
