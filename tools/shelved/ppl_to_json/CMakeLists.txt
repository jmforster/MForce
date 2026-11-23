add_executable(ppl_to_json main.cpp)

# nlohmann::json, as the engine's system folder: a warning inside json.hpp is
# not ours to fail on.
target_link_libraries(ppl_to_json PRIVATE mforce_json)

mforce_compile_rules(ppl_to_json)
