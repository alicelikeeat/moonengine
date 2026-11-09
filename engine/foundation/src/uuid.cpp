#include "aether/foundation/uuid.hpp"

namespace aether {

Uuid Uuid::generate(std::mt19937_64& rng) noexcept {
    Uuid id{rng(), rng()};
    id.hi = (id.hi & ~0xF000ull) | 0x4000ull;                        // version 4
    id.lo = (id.lo & ~(0xC0ull << 56)) | (0x80ull << 56);           // variant 10xx
    return id;
}

std::string Uuid::to_string() const {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    const auto put = [&](std::uint64_t v, int nibbles) {
        for (int i = nibbles - 1; i >= 0; --i) out += kHex[(v >> (i * 4)) & 0xF];
    };
    put(hi >> 32, 8);
    out += '-';
    put(hi >> 16, 4);
    out += '-';
    put(hi, 4);
    out += '-';
    put(lo >> 48, 4);
    out += '-';
    put(lo, 12);
    return out;
}

Result<Uuid> Uuid::parse(std::string_view text) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        return Error{ErrorCode::InvalidArgument, "UUID must have the form xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"};
    Uuid id;
    int nibble = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) continue;
        const char c = text[i];
        std::uint64_t v = 0;
        if (c >= '0' && c <= '9') v = static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v = static_cast<std::uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = static_cast<std::uint64_t>(c - 'A' + 10);
        else return Error{ErrorCode::InvalidArgument, "UUID contains a non-hex character"};
        std::uint64_t& half = nibble < 16 ? id.hi : id.lo;
        half = (half << 4) | v;
        ++nibble;
    }
    return id;
}

}  // namespace aether
