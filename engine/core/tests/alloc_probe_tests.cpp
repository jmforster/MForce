#include "mforce/testing/check.h"
#include "mforce/testing/alloc_probe.h"
#include <vector>

using namespace mforce;


// The allocation probe: counts only while armed, keeps the first armed
// allocation's stack, and resets to nothing.
void run_alloc_probe_tests() {
    alloc_probe::reset();
    { std::vector<int> v(100); (void)v; }                 // unarmed: not counted
    CHECK(alloc_probe::count() == 0);
    alloc_probe::arm();
    { std::vector<int> v(100); (void)v; }                 // one allocation of 400 bytes
    alloc_probe::disarm();
    CHECK(alloc_probe::count() == 1);
    CHECK(alloc_probe::bytes() >= 400);
    int depth = 0;
    const void* const* stack = alloc_probe::first_stack(depth);
    CHECK(stack != nullptr);
#ifdef _WIN32
    CHECK(depth > 0);
#endif
    { std::vector<int> w(10); (void)w; }                  // disarmed again: still 1
    CHECK(alloc_probe::count() == 1);
    alloc_probe::reset();
    CHECK(alloc_probe::count() == 0 && alloc_probe::bytes() == 0);
    alloc_probe::first_stack(depth);
    CHECK(depth == 0);
}
