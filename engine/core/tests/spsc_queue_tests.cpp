#include "mforce/testing/check.h"
#include "mforce/core/spsc_queue.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>

using namespace mforce;

// The queue on one thread (empty, full, order, wrap-around) and between two
// real threads (a million integers, none lost, none repeated, in order).
void run_spsc_queue_tests() {
    std::printf("SpscQueue: empty, full, order, wrap-around; two threads, a million in order\n");
    SpscQueue<int, 4> q;
    int x = -1;
    CHECK(!q.pop(x));
    bool ordered = true;
    for (int round = 0; round < 3; ++round) {                    // past the ring's end: wrap-around
        for (int i = 0; i < 4; ++i) CHECK(q.push(round * 10 + i));
        CHECK(!q.push(99));                                     // full
        for (int i = 0; i < 4; ++i) ordered = ordered && q.pop(x) && x == round * 10 + i;
        CHECK(!q.pop(x));
    }
    CHECK(ordered);
    auto queue = std::make_unique<SpscQueue<std::uint32_t, 1024>>();
    auto& big = *queue;
    const std::uint32_t n = 1000000;
    std::thread producer([&] {
        for (std::uint32_t i = 0; i < n;)
            if (big.push(i)) ++i;
    });
    std::uint32_t expect = 0;
    bool inOrder = true;
    while (expect < n) {
        std::uint32_t v;
        if (!big.pop(v)) continue;
        inOrder = inOrder && v == expect;
        ++expect;
    }
    producer.join();
    std::uint32_t extra;
    CHECK(inOrder && expect == n && !big.pop(extra));
}
