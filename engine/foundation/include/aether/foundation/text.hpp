#pragma once

#include <string>
#include <string_view>

namespace aether {

// Appends `text` to `out` as the body of a JSON string (no surrounding quotes).
void append_json_escaped(std::string& out, std::string_view text);

}  // namespace aether
