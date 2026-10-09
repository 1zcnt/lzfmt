#pragma once

#include "lzfmt.hpp"

namespace lz::fmt {
namespace detail::binary_fmt {

template <typename T>
concept trivially_copyable = std::is_trivially_copyable_v<T>;

// copy + bit_cast instead of reinterpret_cast to stay constexpr;
// gcc optimizes this down to a single memory load/store.
template <trivially_copyable T, typename Cursor, typename E = Cursor::value_type>
    requires trivially_copyable<E> and (sizeof(T) % sizeof(E) == 0)
constexpr void load(T &v, Cursor &r) {
    std::array<E, sizeof(T) / sizeof(E)> buf;
    r.read(buf.data(), buf.size());
    v = std::bit_cast<T>(buf);
}

template <trivially_copyable T, typename Cursor, typename E = Cursor::value_type>
    requires trivially_copyable<E> and (sizeof(T) % sizeof(E) == 0)
constexpr void store(const T &v, Cursor &r) {
    auto buf = std::bit_cast<std::array<E, sizeof(T) / sizeof(E)>>(v);
    r.write(buf.data(), buf.size());
}

template <auto P, primitive<P> T>
struct [[=priority::impl(0)]] primitive_formatter : base_primitive_formatter<P, T> {
    template <typename Ctx>
        requires requires(T &v, Ctx ctx) { load(v, ctx.r); }
    static constexpr void read(T &v, Ctx ctx) {
        load(v, ctx.r);
    }

    template <typename Ctx>
        requires requires(const T &v, Ctx ctx) { store(v, ctx.r); }
    static constexpr void write(const T &v, Ctx ctx) {
        store(v, ctx.r);
    }
};

template <auto P, std::ranges::range T>
struct [[=priority::impl(1)]] range_formatter : base_range_formatter<P, T> {
    using E = std::ranges::range_value_t<T>;

    // numbers stored as in memory need no formatter each
    template <typename Ctx, typename EF = [: Ctx::root::formatter_for(^^E) :]>
    static constexpr bool raw = requires { requires EF::in_memory; } and
                                requires(E &e, Ctx ctx) { load(e, ctx.r); };

    template <typename Ctx>
    static constexpr void read(T &v, Ctx ctx) {
        for (auto &e : v) {
            if constexpr (raw<Ctx>) load(e, ctx.r);
            else                    ctx.read(e);
        }
    }

    template <typename Ctx>
    static constexpr void write(const T &v, Ctx ctx) {
        for (const auto &e : v) {
            if constexpr (raw<Ctx>) store(e, ctx.r);
            else                    ctx.write(e);
        }
    }
};

} // namespace detail::binary_fmt

// the built-in formatters, to derive user formatters from.
// a formatter deriving from one is of mid priority unless annotated otherwise
template <auto P, typename T>
using range_formatter = detail::binary_fmt::range_formatter<P, T>;

template <auto P, typename T>
using primitive_formatter = detail::binary_fmt::primitive_formatter<P, T>;

template <auto P, typename T>
using class_formatter = detail::base_class_formatter<P, T>;

// the binary format of params P: UserFmts take over from the built-in formatters.
//   binfmt<P>::read(v, sb), binfmt<P>::write(v, sb) return the bytes read or written
template <auto P, template <auto, typename> typename ...UserFmts>
using binfmt = detail::formatter<P,
    UserFmts...,
    detail::binary_fmt::range_formatter,
    detail::binary_fmt::primitive_formatter,
    detail::base_class_formatter>;

// a named list of formatters, so each header can declare its own:
//   using net_formatters = lz::fmt::formatters<packet_formatter, header_formatter>;
//   using fmt = lz::fmt::binfmt_of<P, net_formatters, db_formatters>;
// ties between them go to the one listed first, as in binfmt
template <template <auto, typename> typename ...Fmts>
struct formatters { };

namespace detail {
template <auto P, typename ...Sets>
struct binfmt_of;

template <auto P, template <auto, typename> typename ...Fmts>
struct binfmt_of<P, formatters<Fmts...>> {
    using type = binfmt<P, Fmts...>;
};

template <auto P, template <auto, typename> typename ...A,
                  template <auto, typename> typename ...B, typename ...Rest>
struct binfmt_of<P, formatters<A...>, formatters<B...>, Rest...>
    : binfmt_of<P, formatters<A..., B...>, Rest...> { };
} // namespace detail

template <auto P, typename ...Sets>
using binfmt_of = detail::binfmt_of<P, formatters<>, Sets...>::type;

} // namespace lz::fmt
