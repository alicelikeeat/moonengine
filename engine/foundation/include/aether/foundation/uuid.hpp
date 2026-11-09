// 128-bit universally unique identifier (RFC 9562 version 4, random).
//
// Used as the persistent identity of scenes, entities and assets. Runtime
// handles (such as aether::world::Entity) are fast but change between runs;
// a Uuid is what gets written to disk and referenced over ACP.
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <string_view>

#include "aether/foundation/result.hpp"

namespace aether {

struct Uuid {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;

    [[nodiscard]] constexpr bool is_nil() const noexcept { return hi == 0 && lo == 0; }
    friend constexpr auto operator<=>(const Uuid&, const Uuid&) = default;

    // Random version-4 UUID drawn from `rng`. Seed the engine deterministically
    // for reproducible tests and replays.
    [[nodiscard]] static Uuid generate(std::mt19937_64& rng) noexcept;

    // Canonical lowercase form: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    [[nodiscard]] std::string to_string() const;
    // Accepts the canonical form in either case.
    [[nodiscard]] static Result<Uuid> parse(std::string_view text);
};

struct UuidHash {
    [[nodiscard]] std::size_t operator()(const Uuid& id) const noexcept {
        // Random UUIDs are already well mixed; fold the halves.
        return static_cast<std::size_t>(id.hi ^ (id.lo * 0x9E3779B97F4A7C15ull));
    }
};

}  // namespace aether
