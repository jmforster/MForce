add_executable(durn_converter main.cpp)

# Header-only — no library dependency needed (uses research parsers directly)
target_include_directories(durn_converter PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)

if (MSVC)
  target_compile_options(durn_converter PRIVATE /W4 /permissive-)
endif()

set_target_properties(durn_converter PROPERTIES CXX_STANDARD 17)
