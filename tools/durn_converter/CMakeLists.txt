add_executable(durn_converter main.cpp)

# Links no engine library: its parsers (parsers/) are header-only and use no engine header.
mforce_compile_rules(durn_converter)
