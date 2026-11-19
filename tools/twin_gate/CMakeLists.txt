# The twin gate: each patch file through the loader and through the module in
# one process, compared sample for sample as floats on the offline and the
# live path, with the allocation count (main.cpp). Links the probe, so this
# executable's operator new is the counting one.
add_executable(twin_gate main.cpp)
target_link_libraries(twin_gate PRIVATE mforce_engine mforce_alloc_probe)
if (MSVC)
  target_compile_options(twin_gate PRIVATE /W4 /permissive-)
endif()
