add_executable(mforce_keys main.cpp)
target_link_libraries(mforce_keys PRIVATE mforce_engine winmm)

if (MSVC)
  target_compile_options(mforce_keys PRIVATE /W4 /permissive-)
endif()
