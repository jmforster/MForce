add_executable(ppl_to_json main.cpp)

# Header-only nlohmann::json lives at engine/third_party/
target_include_directories(ppl_to_json PRIVATE
  ${CMAKE_SOURCE_DIR}/engine/include
  ${CMAKE_SOURCE_DIR}/engine/third_party
)

if (MSVC)
  target_compile_options(ppl_to_json PRIVATE /W4 /permissive-)
endif()

set_target_properties(ppl_to_json PROPERTIES CXX_STANDARD 17)
