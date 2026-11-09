#include <cstdio>
#include <random>
#include <string>

#include "aether/foundation/json.hpp"
#include "aether_test.hpp"

using namespace aether;

AETHER_TEST(json_parses_all_value_types) {
    auto r = parse_json(R"({"a": 1, "b": [true, false, null], "c": "x", "d": {"e": -2.5e3}})");
    REQUIRE(r.has_value());
    const Json& j = r.value();
    REQUIRE(j.is_object());
    CHECK(j.find("a")->as_number() == 1.0);
    CHECK(j.find("b")->as_array().size() == 3);
    CHECK(j.find("b")->as_array()[0].as_bool());
    CHECK(j.find("b")->as_array()[2].is_null());
    CHECK_EQ(j.find("c")->as_string(), std::string("x"));
    CHECK(j.find("d")->find("e")->as_number() == -2500.0);
    CHECK(j.find("missing") == nullptr);
    CHECK_EQ(j.as_object()[0].key, std::string("a"));  // order kept
}

AETHER_TEST(json_string_escapes_and_unicode) {
    auto r = parse_json(R"("q\" bs\\ nl\n tab\t u\u00e9 cjk\u5f15 emoji\ud83d\ude00")");
    REQUIRE(r.has_value());
    CHECK_EQ(r.value().as_string(), std::string("q\" bs\\ nl\n tab\t u\xC3\xA9 cjk\xE5\xBC\x95 emoji\xF0\x9F\x98\x80"));
    // Raw UTF-8 passes through unchanged.
    auto raw = parse_json("\"\xE5\xBC\x95\xE6\x93\x8E\"");
    REQUIRE(raw.has_value());
    CHECK_EQ(raw.value().as_string(), std::string("\xE5\xBC\x95\xE6\x93\x8E"));
}

AETHER_TEST(json_round_trips_compact_and_pretty) {
    Json doc = Json::object();
    doc.set("name", "Sun \"main\"\n");
    doc.set("pi", 3.141592653589793);
    doc.set("tiny", 1e-300);
    doc.set("int", 42);
    Json arr = Json::array();
    arr.push_back(0.1);
    arr.push_back(-0.0);
    arr.push_back(Json::object());
    doc.set("list", arr);
    for (const int indent : {-1, 2}) {
        const std::string text = to_json_string(doc, indent);
        auto back = parse_json(text);
        REQUIRE(back.has_value());
        CHECK(back.value() == doc);  // exact, including 0.1 and 1e-300
    }
    CHECK_EQ(to_json_string(Json::array()), std::string("[]"));
    CHECK_EQ(to_json_string(Json(0.1)), std::string("0.1"));
    CHECK_EQ(to_json_string(Json(-0.0)), std::string("0"));
}

AETHER_TEST(json_set_replaces_existing_key) {
    Json o = Json::object();
    o.set("k", 1);
    o.set("k", 2);
    CHECK_EQ(o.as_object().size(), std::size_t{1});
    CHECK(o.find("k")->as_number() == 2.0);
}

AETHER_TEST(json_rejects_malformed_input_with_position) {
    const char* bad[] = {
        "", "{", "[1,]", "{\"a\":1,}", "{\"a\" 1}", "[1 2]", "01", "1.", "1e", "-", "+1", ".5",
        "\"unterminated", "\"bad \\x escape\"", "\"\\ud800\"", "\"\\udc00\"", "nul", "tru", "[1]x",
        "{\"a\":1,\"a\":2}", "1e999", "\"tab\there\"", "{1:2}", "[\"\\u12\"]",
    };
    for (const char* text : bad) {
        auto r = parse_json(text);
        if (r.has_value()) std::printf("    unexpectedly parsed: %s\n", text);
        CHECK(!r.has_value());
    }
    auto r = parse_json("{\n  \"a\": [1,\n  2,,]\n}");
    REQUIRE(!r);
    CHECK(r.error().message.find("line 3") != std::string::npos);
}

AETHER_TEST(json_enforces_depth_and_size_limits) {
    const std::string deep = std::string(10'000, '[') + std::string(10'000, ']');
    auto r = parse_json(deep);
    REQUIRE(!r);
    CHECK(r.error().message.find("too deep") != std::string::npos);
    auto ok = parse_json(std::string(100, '[') + std::string(100, ']'));
    CHECK(ok.has_value());
    CHECK(!parse_json("[1,2,3]", JsonParseOptions{256, 4}).has_value());
}

AETHER_TEST(json_fuzz_mutations_never_crash) {
    const std::string seed = R"({"format":"aether.level","version":1,"entities":[{"id":"x","components":{"Name":{"value":"Sun\u00e9"},"Transform":{"position":[1,2.5,-3e2]}}}],"flag":true,"n":null})";
    const std::string alphabet = "{}[]\",:\\/ 0123456789.eE+-truefalsnu\x01\xff";
    std::mt19937 rng(1234);
    int parsed = 0;
    for (int i = 0; i < 20'000; ++i) {
        std::string s = seed;
        const int edits = 1 + static_cast<int>(rng() % 4);
        for (int k = 0; k < edits; ++k) {
            const std::size_t pos = rng() % (s.size() + 1);
            switch (rng() % 3) {
                case 0: if (!s.empty() && pos < s.size()) s.erase(pos, 1); break;
                case 1: s.insert(pos, 1, alphabet[rng() % alphabet.size()]); break;
                default: if (pos < s.size()) s[pos] = alphabet[rng() % alphabet.size()]; break;
            }
        }
        auto r = parse_json(s);
        if (r) {
            ++parsed;
            // Anything accepted must re-serialise to something that parses equal.
            auto again = parse_json(to_json_string(r.value()));
            REQUIRE(again.has_value());
            CHECK(again.value() == r.value());
        }
    }
    std::printf("    %d of 20000 mutated inputs were still valid JSON\n", parsed);
}
