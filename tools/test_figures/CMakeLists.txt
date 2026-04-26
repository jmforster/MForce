add_executable(test_figures main.cpp)
target_link_libraries(test_figures PRIVATE mforce_engine)

if (MSVC)
  target_compile_options(test_figures PRIVATE /W4 /permissive-)
endif()
