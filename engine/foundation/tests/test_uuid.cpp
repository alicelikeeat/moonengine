#include <random>
#include <string>
#include <unordered_set>

#include "aether/foundation/uuid.hpp"
#include "aether_test.hpp"

using namespace aether;

AETHER_TEST(uuid_generates_v4_and_unique) {
    std::mt19937_64 rng(42);
    std::unordered_set<Uuid, UuidHash> seen;
    for (int i = 0; i < 10'000; ++i) {
        const Uuid id = Uuid::generate(rng);
        CHECK(!id.is_nil());
        CHECK_EQ((id.hi >> 12) & 0xF, std::uint64_t{4});   // version
        CHECK_EQ(id.lo >> 62, std::uint64_t{2});           // variant
        seen.insert(id);
    }
    CHECK_EQ(seen.size(), std::size_t{10'000});
}

AETHER_TEST(uuid_is_deterministic_for_a_seed) {
    std::mt19937_64 a(7), b(7);
    CHECK(Uuid::generate(a) == Uuid::generate(b));
}

AETHER_TEST(uuid_round_trips_through_text) {
    std::mt19937_64 rng(1);
    for (int i = 0; i < 100; ++i) {
        const Uuid id = Uuid::generate(rng);
        const std::string text = id.to_string();
        CHECK_EQ(text.size(), std::size_t{36});
        auto parsed = Uuid::parse(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed.value() == id);
    }
    auto upper = Uuid::parse("123E4567-E89B-42D3-A456-426614174000");
    REQUIRE(upper.has_value());
    CHECK_EQ(upper.value().to_string(), std::string("123e4567-e89b-42d3-a456-426614174000"));
}

AETHER_TEST(uuid_rejects_malformed_text) {
    CHECK(!Uuid::parse(""));
    CHECK(!Uuid::parse("123e4567e89b42d3a456426614174000"));
    CHECK(!Uuid::parse("123e4567-e89b-42d3-a456-42661417400g"));
    CHECK(!Uuid::parse("123e4567-e89b-42d3-a456-4266141740000"));
}
