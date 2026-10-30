// The counting allocator. Defining the replaceable allocation functions in
// any translation unit of a program replaces the library's for that whole
// program, so this file is linked only into the executables that test the
// engine (alloc_gate, engine_tests) and never into the engine, the CLI or
// the UI. Every form forwards to the C allocator; while armed, each one is
// counted and the first one's return addresses are kept. Nothing here
// allocates.
#include "alloc_probe.h"

#include <cstdlib>
#include <new>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>   // CaptureStackBackTrace
#endif

namespace {

constexpr int kMaxDepth = 32;

thread_local bool        g_armed = false;
thread_local std::size_t g_count = 0;
thread_local std::size_t g_bytes = 0;
thread_local void*       g_stack[kMaxDepth];
thread_local int         g_depth = 0;

void record(std::size_t size) noexcept {
    if (!g_armed) return;
    ++g_count;
    g_bytes += size;
    if (g_count == 1) {
#ifdef _WIN32
        g_depth = int(CaptureStackBackTrace(1, kMaxDepth, g_stack, nullptr));
#else
        g_depth = 0;
#endif
    }
}

void* allocate(std::size_t size) {
    record(size);
    if (size == 0) size = 1;
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}

void* allocate_aligned(std::size_t size, std::size_t alignment) {
    record(size);
    if (size == 0) size = 1;
#ifdef _WIN32
    void* p = _aligned_malloc(size, alignment);
#else
    void* p = std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
#endif
    if (p) return p;
    throw std::bad_alloc();
}

void release(void* p) noexcept { std::free(p); }

void release_aligned(void* p) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}

}  // namespace

namespace alloc_probe {

void arm() { g_armed = true; }
void disarm() { g_armed = false; }
bool armed() { return g_armed; }
std::size_t count() { return g_count; }
std::size_t bytes() { return g_bytes; }
const void* const* first_stack(int& depth) { depth = g_depth; return g_stack; }
void reset() { g_count = 0; g_bytes = 0; g_depth = 0; }

}  // namespace alloc_probe

// The replaceable allocation functions ([new.delete] in the standard): the
// plain, array, nothrow and aligned forms of new, and every delete that
// pairs with one of them. An aligned allocation is released by the aligned
// release; the two families never mix.

void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return allocate(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return allocate(n); } catch (...) { return nullptr; }
}
void* operator new(std::size_t n, std::align_val_t al) { return allocate_aligned(n, std::size_t(al)); }
void* operator new[](std::size_t n, std::align_val_t al) { return allocate_aligned(n, std::size_t(al)); }
void* operator new(std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept {
    try { return allocate_aligned(n, std::size_t(al)); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept {
    try { return allocate_aligned(n, std::size_t(al)); } catch (...) { return nullptr; }
}

void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete(void* p, std::align_val_t) noexcept { release_aligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { release_aligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { release_aligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { release_aligned(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { release_aligned(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { release_aligned(p); }
