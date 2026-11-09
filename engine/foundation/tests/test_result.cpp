#include <string>

#include "aether/foundation/result.hpp"
#include "aether_test.hpp"

using namespace aether;

namespace {
Result<int> parse_positive(int v) {
    if (v <= 0) return Error{ErrorCode::InvalidArgument, "must be positive"};
    return v;
}
}  // namespace

AETHER_TEST(result_holds_value) {
    auto r = parse_positive(5);
    REQUIRE(r.has_value());
    CHECK_EQ(r.value(), 5);
    CHECK(static_cast<bool>(r));
}

AETHER_TEST(result_holds_error) {
    auto r = parse_positive(-1);
    REQUIRE(!r);
    CHECK(r.error().code == ErrorCode::InvalidArgument);
    CHECK_EQ(r.error().message, std::string("must be positive"));
    CHECK_EQ(r.value_or(7), 7);
}

AETHER_TEST(result_void) {
    Result<void> good = ok();
    CHECK(good.has_value());
    Result<void> bad = Error{ErrorCode::NotFound, "missing"};
    REQUIRE(!bad);
    CHECK(bad.error().code == ErrorCode::NotFound);
}

AETHER_TEST(result_move_only_value) {
    Result<std::string> r = std::string(100, 'x');
    std::string taken = std::move(r).value();
    CHECK_EQ(taken.size(), std::size_t{100});
}

AETHER_TEST(error_code_names_are_stable) {
    CHECK_EQ(to_string(ErrorCode::Conflict), std::string_view("conflict"));
    CHECK_EQ(static_cast<unsigned>(ErrorCode::DeviceLost), 13u);
}
