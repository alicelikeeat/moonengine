#include <vector>

#include "aether/foundation/lifecycle.hpp"
#include "aether_test.hpp"

using namespace aether;

AETHER_TEST(lifecycle_happy_path) {
    Lifecycle lc;
    std::vector<LifecycleState> seen;
    lc.on_transition([&](LifecycleState, LifecycleState to) { seen.push_back(to); });
    CHECK(lc.begin_init().has_value());
    CHECK(lc.finish_init().has_value());
    CHECK(lc.pause().has_value());
    CHECK(lc.resume().has_value());
    CHECK(lc.device_lost().has_value());
    CHECK(lc.device_recovered().has_value());
    CHECK(lc.begin_shutdown().has_value());
    CHECK(lc.finish_shutdown().has_value());
    CHECK(lc.state() == LifecycleState::Stopped);
    CHECK_EQ(seen.size(), std::size_t{8});
}

AETHER_TEST(lifecycle_rejects_invalid_transitions) {
    Lifecycle lc;
    auto r = lc.pause();
    REQUIRE(!r);
    CHECK(r.error().code == ErrorCode::InvalidState);
    CHECK(lc.state() == LifecycleState::Uninitialized);
    CHECK(lc.begin_init().has_value());
    CHECK(!lc.begin_init());
    CHECK(lc.begin_shutdown().has_value());
    CHECK(lc.finish_shutdown().has_value());
    CHECK(!lc.begin_init());  // Stopped is terminal
}
