add_executable(durn_converter main.cpp)

# Header-only — no library dependency needed (uses research parsers directly)
mforce_compile_rules(durn_converter)
