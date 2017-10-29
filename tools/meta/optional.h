#pragma once
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace meta {

/// extended optional based on std::optional
// simple encapsulation to add map functionality
// not all features are delegated!
template<class T>
class optional {
    std::optional<T> m;

public:
    constexpr optional()
        : m{} {}
    constexpr optional(const T &t)
        : m{t} {}

    constexpr optional(const optional &) = default;
    constexpr optional &operator=(const optional &) = default;
    constexpr optional(optional &&) = default;
    constexpr optional &operator=(optional &&) = default;

    constexpr operator bool() const { return m.has_value(); }
    constexpr const T &value() const { return m.value(); }
    constexpr T &value() { return m.value(); }

    /// encapsulate the condition
    template<class F>
    constexpr auto map(F &&f) const -> decltype(f(value())) {
        if (m.has_value()) return f(value());
        if constexpr (!std::is_void_v<decltype(f(value()))>) return {};
    }

    constexpr bool operator==(const optional &o) const { return m == o.m; }
    constexpr bool operator!=(const optional &o) const { return m != o.m; }
};

/// compile time pair of a type and value
// we use a constexpr functor F to pass the value.
// because passing custom struct values to templates is not allowed!
template<class T, class TF>
struct type_value {
    using type = T;
    static constexpr T value = TF{}();
};

/// tag type to trigger a value packed optional implementation
template<class T>
struct packed;

/// very simple value packed optional specialization
// keep the API in sync with above optional
template<class T, class TF>
class optional<packed<type_value<T, TF>>> {
    T data;

public:
    constexpr optional()
        : data(TF{}()) {}
    constexpr optional(const T &t)
        : data(t) {}

    constexpr optional(const optional &) = default;
    constexpr optional &operator=(const optional &) = default;
    constexpr optional(optional &&) = default;
    constexpr optional &operator=(optional &&) = default;

    constexpr operator bool() const { return !(data == TF{}()); }
    constexpr const T &value() const { return data; }
    constexpr T &value() { return data; }

    /// encapsulate the condition
    template<class F>
    constexpr auto map(F &&f) const -> decltype(f(value())) {
        if (*this) return f(value());
        if constexpr (!std::is_void_v<decltype(f(value()))>) return {};
    }

    constexpr bool operator==(const optional &o) const { return data == o.data; }
    constexpr bool operator!=(const optional &o) const { return data != o.data; }
};

template<class T>
struct default_invalid_t {
    constexpr T operator()() { return {}; }
};

/// convenience overload for default initialized invalid values
template<class T>
class optional<packed<T>> : public optional<packed<type_value<T, default_invalid_t<T>>>> {
    using base_t = optional<packed<type_value<T, default_invalid_t<T>>>>;
    using optional<packed<type_value<T, default_invalid_t<T>>>>::optional;
};

/// bool with map functionality
using optional_bool_t = optional<packed<bool>>;

} // namespace meta
