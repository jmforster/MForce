# The one test harness (check.h, run_tests) and the counting allocator every
# test program may use. It uses no engine module.
#
# alloc_probe.cpp stays one object of its own: it holds both the arm/count API
# and the replacement operator new/delete, so a program that calls the API
# pulls the replacement with it, and a program that does not keeps the
# library's allocator.
mforce_module(testing
  SOURCES src/check.cpp
          src/alloc_probe.cpp)

# The harness's own test: a failed CHECK, a throw and a failed REQUIRE.
mforce_tests(testing SOURCES tests/main.cpp)
