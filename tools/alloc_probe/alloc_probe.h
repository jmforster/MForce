#pragma once
#include <cstddef>

// A counting allocator for the executables that test the engine. Linking
// mforce_alloc_probe replaces the program's global operator new and delete
// (alloc_probe.cpp); between arm() and disarm() every allocation on the
// calling thread is counted, and the first one's return addresses are kept
// for the host to symbolize. The engine, the CLI and the UI never link it.
namespace alloc_probe {

void arm();                                   // count from here, on this thread
void disarm();                                // stop counting; the count and the first stack stay
bool armed();
std::size_t count();                          // armed allocations since the last reset
std::size_t bytes();                          // their requested sizes, summed
const void* const* first_stack(int& depth);   // return addresses of the first armed allocation; depth 0 when none
void reset();                                 // count 0, bytes 0, no stack

}  // namespace alloc_probe
