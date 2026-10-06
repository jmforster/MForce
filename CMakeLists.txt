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

# RtAudio — callback-based audio I/O for live keyboard play in mforce_ui.
# Static lib, WASAPI on Windows (default), no tests, no python bindings.
# RTAUDIO_STATIC_MSVCRT OFF so it uses /MD like our other libs (avoids
# MT/MD mismatch linker warnings).
set(RTAUDIO_BUILD_STATIC_LIBS ON  CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_TESTING     OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_PYTHON      OFF CACHE BOOL "" FORCE)
set(RTAUDIO_STATIC_MSVCRT     OFF CACHE BOOL "" FORCE)
add_subdirectory(engine/third_party/rtaudio)

# RtMidi — MIDI keyboard input for mforce_ui. Static lib, WinMM on Windows
# (default), no tests.
set(RTMIDI_BUILD_STATIC_LIBS ON  CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_TESTING     OFF CACHE BOOL "" FORCE)
add_subdirectory(engine/third_party/rtmidi)

add_subdirectory(engine)
add_subdirectory(tools/mforce_cli)
add_subdirectory(tools/mforce_ui)
add_subdirectory(tools/durn_converter)
add_subdirectory(tools/test_figures)
add_subdirectory(tools/ppl_to_json)
add_subdirectory(tools/stamp_test)
add_subdirectory(tools/engine_tests)

# Tests. Every test runs from the repo root: the test programs load patches
# and scores by repo-relative path.
enable_testing()
add_test(NAME engine_tests COMMAND engine_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME test_figures COMMAND test_figures WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
# The interpreter on PATH (where lizard is installed), not the newest one in the Windows registry.
set(Python3_FIND_STRATEGY LOCATION)
set(Python3_FIND_REGISTRY NEVER)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME structure_meter_tests
             COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tools/structure/tests -p "test_*.py"
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_test(NAME structure_meter
             COMMAND ${Python3_EXECUTABLE} tools/structure/check.py
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
    message(WARNING "Python3 not found: the structure meter tests are not registered")
endif()
