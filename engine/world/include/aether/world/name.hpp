// Human-readable entity label, shown in tools (outliner, logs, ACP).
// Names need not be unique; identity is the entity's Uuid.
#pragma once

#include <string>
#include <string_view>

namespace aether::world {

struct Name {
    static constexpr std::string_view kComponentName = "Name";
    std::string value;
};

}  // namespace aether::world
