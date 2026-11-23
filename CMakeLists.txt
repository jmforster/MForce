cmake_minimum_required(VERSION 3.20)
project(MForce LANGUAGES CXX)

# The compile rules every MForce target takes (mforce_compile_rules).
include(cmake/MforceModule.cmake)

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

add_subdirectory(engine)
add_subdirectory(tools/mforce_cli)
add_subdirectory(tools/durn_converter)
add_subdirectory(tools/test_figures)
add_subdirectory(tools/shelved/ppl_to_json)
add_subdirectory(tools/engine_tests)
add_subdirectory(tools/alloc_probe)
add_subdirectory(tools/alloc_gate)

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
    add_test(NAME tool_tests
             COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tools/tests -p "test_*.py"
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    # The tool tests that run mforce_cli are given this configuration's binary.
    set_tests_properties(tool_tests PROPERTIES ENVIRONMENT "MFORCE_CLI=$<TARGET_FILE:mforce_cli>")
else()
    message(WARNING "Python3 not found: the structure meter tests are not registered")
endif()
