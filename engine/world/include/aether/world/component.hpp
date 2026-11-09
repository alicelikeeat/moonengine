// Component type metadata.
//
// Any nothrow-movable object type can be a component. Give it a stable,
// human-readable name for tools, serialization and AI agents (ACP) with:
//
//   struct Velocity {
//       static constexpr std::string_view kComponentName = "Velocity";
//       float x, y, z;
//   };
//
// Without kComponentName the compiler's RTTI name is used, which differs
// between compilers and must not be persisted.
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace aether::world {

using ComponentId = std::uint32_t;
inline constexpr ComponentId kInvalidComponent = 0xFFFF'FFFFu;

template <typename T>
concept Component = std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> &&
                    !std::is_array_v<T> && std::is_nothrow_move_constructible_v<T> &&
                    std::is_nothrow_destructible_v<T>;

template <typename T>
concept NamedComponent = requires {
    { T::kComponentName } -> std::convertible_to<std::string_view>;
};

struct ComponentInfo {
    std::string name;
    std::size_t size = 0;
    std::size_t align = 0;
    bool stable_name = false;  // true when the name came from kComponentName
    void (*move_construct)(void* dst, void* src) noexcept = nullptr;
    void (*destroy)(void* object) noexcept = nullptr;
};

namespace detail {
// One distinct address per type; used as a registry key. Not an
// inter-module communication channel (see ADR 0003).
template <typename T>
inline constexpr char kTypeKey = 0;

template <typename T>
[[nodiscard]] constexpr const void* type_key() noexcept { return &kTypeKey<T>; }
}  // namespace detail

template <Component T>
[[nodiscard]] ComponentInfo make_component_info() {
    ComponentInfo info;
    if constexpr (NamedComponent<T>) {
        info.name = std::string(std::string_view(T::kComponentName));
        info.stable_name = true;
    } else {
        info.name = typeid(T).name();
    }
    info.size = sizeof(T);
    info.align = alignof(T);
    info.move_construct = [](void* dst, void* src) noexcept {
        ::new (dst) T(std::move(*static_cast<T*>(src)));
    };
    info.destroy = [](void* object) noexcept { static_cast<T*>(object)->~T(); };
    return info;
}

}  // namespace aether::world
