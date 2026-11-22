# The counting allocator (alloc_probe.h). A static library whose one object
# holds both the arm/count API and the replacement operator new/delete, so
# that an executable pulling the API pulls the replacement with it. Linked by
# alloc_gate and engine_tests only.
add_library(mforce_alloc_probe STATIC alloc_probe.cpp)
target_include_directories(mforce_alloc_probe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (MSVC)
  target_compile_options(mforce_alloc_probe PRIVATE /W4 /permissive-)
endif()
