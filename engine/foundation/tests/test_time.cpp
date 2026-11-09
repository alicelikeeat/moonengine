#include "aether/foundation/time.hpp"
#include "aether_test.hpp"

using namespace aether;

AETHER_TEST(fixed_step_accumulates) {
    FixedTimestep ts(10, 8);
    auto r = ts.advance(25);
    CHECK_EQ(r.steps, 2u);
    CHECK(r.alpha > 0.49 && r.alpha < 0.51);
    r = ts.advance(5);
    CHECK_EQ(r.steps, 1u);
    CHECK(r.alpha == 0.0);
    CHECK_EQ(ts.total_steps(), std::uint64_t{3});
    CHECK(!r.clamped);
}

AETHER_TEST(fixed_step_clamps_long_frames) {
    FixedTimestep ts(10, 4);
    auto r = ts.advance(1005);
    CHECK_EQ(r.steps, 4u);
    CHECK(r.clamped);
    CHECK(r.alpha > 0.49 && r.alpha < 0.51);
}

AETHER_TEST(fixed_step_is_deterministic) {
    const std::uint64_t frames[] = {16'000'000, 17'500'000, 33'000'000, 1'000'000, 16'666'667, 50'000'000};
    FixedTimestep a, b;
    for (int run = 0; run < 3; ++run) {
        for (auto f : frames) {
            const auto ra = a.advance(f);
            const auto rb = b.advance(f);
            CHECK_EQ(ra.steps, rb.steps);
            CHECK(ra.alpha == rb.alpha);
        }
    }
    CHECK_EQ(a.total_steps(), b.total_steps());
}

AETHER_TEST(stopwatch_is_monotonic) {
    Stopwatch sw;
    const auto t0 = sw.elapsed_ns();
    const auto t1 = sw.elapsed_ns();
    CHECK(t1 >= t0);
}
