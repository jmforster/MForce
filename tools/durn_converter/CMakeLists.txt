add_executable(durn_converter main.cpp)

# Header-only — no library dependency needed (uses research parsers directly)
target_include_directories(durn_converter PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)

mforce_compile_rules(durn_converter)
