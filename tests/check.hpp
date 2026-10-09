#pragma once

#include <lzfmt/binfmt.hpp>
#include <lzfmt/util.hpp>

#include <cstdio>

inline int failures = 0;

#define CHECK(...)                                                                  \
    do {                                                                            \
        if (!(__VA_ARGS__)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,   \
                         #__VA_ARGS__);                                             \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

#define CHECK_THROWS(E, ...)                                                        \
    do {                                                                            \
        bool thrown_ = false;                                                       \
        try { __VA_ARGS__; } catch (const E &) { thrown_ = true; }                  \
        if (!thrown_) {                                                             \
            std::fprintf(stderr, "%s:%d: %s didn't throw %s\n", __FILE__, __LINE__, \
                         #__VA_ARGS__, #E);                                         \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

struct params {
    std::endian endianness       = std::endian::little;
    bool        enum_is_integral = true;
};

using le = lz::fmt::binfmt<params{}>;
using be = lz::fmt::binfmt<params{std::endian::big}>;

// the stream buffer of a range, a stream or a stream buffer.
// one over a temporary range lasts until the end of the full expression
template <typename S>
constexpr auto sb(S &&s) -> decltype(lz::fmt::stream_buffer(s)) {
    return lz::fmt::stream_buffer(s);
}

template <typename F, typename T, typename S>
concept can_parse = requires(T &v, S &s) { F::read(v, sb(s)); };

template <typename F, typename T, typename S>
concept can_format = requires(const T &v, S &s) { F::write(v, sb(s)); };

template <typename... T>
constexpr auto bytes(T... v) {
    return std::array<std::uint8_t, sizeof...(T)>{static_cast<std::uint8_t>(v)...};
}
