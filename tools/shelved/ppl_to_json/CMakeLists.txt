add_executable(ppl_to_json main.cpp)

target_include_directories(ppl_to_json PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/include
)
# Header-only nlohmann::json lives at engine/third_party/, a SYSTEM folder so
# that a warning inside json.hpp is not ours to fail on.
target_include_directories(ppl_to_json SYSTEM PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/third_party
)

mforce_compile_rules(ppl_to_json)
