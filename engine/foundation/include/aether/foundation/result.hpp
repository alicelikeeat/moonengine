// Unified error and result types for Aether Engine.
//
// Engine code reports recoverable failures through Result<T> instead of
// exceptions so that errors cross module, language and process boundaries
// with a stable, documented shape (see protocols/acp for the wire form).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "aether/foundation/assert.hpp"

namespace aether {

// Stable numeric codes. Values are part of the public contract and must not
// be renumbered; append new codes at the end of their group.
enum class ErrorCode : std::uint32_t {
    Ok = 0,
    Unknown = 1,
    InvalidArgument = 2,
    InvalidState = 3,
    NotFound = 4,
    AlreadyExists = 5,
    OutOfMemory = 6,
    Unsupported = 7,
    Timeout = 8,
    Cancelled = 9,
    PermissionDenied = 10,
    Conflict = 11,
    IoError = 12,
    DeviceLost = 13,
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string message;

    Error() = default;
    Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
};

template <typename T>
class [[nodiscard]] Result {
    static_assert(!std::is_reference_v<T>, "Result<T&> is not supported");
    static_assert(!std::is_same_v<std::remove_cv_t<T>, Error>, "Result<Error> is ambiguous");

public:
    Result(const T& value) : storage_(std::in_place_index<0>, value) {}
    Result(T&& value) : storage_(std::in_place_index<0>, std::move(value)) {}
    Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

    [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & {
        AETHER_ASSERT(has_value(), "Result::value() called on an error");
        return std::get<0>(storage_);
    }
    [[nodiscard]] const T& value() const& {
        AETHER_ASSERT(has_value(), "Result::value() called on an error");
        return std::get<0>(storage_);
    }
    [[nodiscard]] T&& value() && {
        AETHER_ASSERT(has_value(), "Result::value() called on an error");
        return std::get<0>(std::move(storage_));
    }

    [[nodiscard]] const Error& error() const& {
        AETHER_ASSERT(!has_value(), "Result::error() called on a value");
        return std::get<1>(storage_);
    }

    template <typename U>
    [[nodiscard]] T value_or(U&& fallback) const& {
        return has_value() ? std::get<0>(storage_) : static_cast<T>(std::forward<U>(fallback));
    }

private:
    std::variant<T, Error> storage_;
};

template <>
class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)), ok_(false) {}

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    [[nodiscard]] const Error& error() const& {
        AETHER_ASSERT(!ok_, "Result::error() called on success");
        return error_;
    }

private:
    Error error_{ErrorCode::Ok, {}};
    bool ok_ = true;
};

[[nodiscard]] inline Result<void> ok() { return {}; }

}  // namespace aether
