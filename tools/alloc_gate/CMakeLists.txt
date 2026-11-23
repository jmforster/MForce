# The allocation gate: a patch or a folder, the allocations between note-on
# and the last sample, and where the first one came from (main.cpp). Links the
# probe, so this executable's operator new is the counting one.
add_executable(alloc_gate main.cpp)
target_link_libraries(alloc_gate PRIVATE mforce_engine mforce_alloc_probe)
mforce_compile_rules(alloc_gate)
if (MSVC)
  target_link_libraries(alloc_gate PRIVATE dbghelp)
endif()
