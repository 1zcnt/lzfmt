#pragma once

#include "lzfmt.hpp"

namespace lz::fmt {
namespace detail::binary_fmt {

template <typename T>
concept trivially_copyable = std::is_trivially_copyable_v<T>;

template <typename Rg>
concept input_buffer = std::ranges::input_range<Rg> and trivially_copyable<buffer_element_t<Rg>>;

template <typename Rg>
concept output_buffer = std::ranges::output_range<Rg, buffer_element_t<Rg>> and
                        trivially_copyable<buffer_element_t<Rg>>;

template <fmt_direction D, trivially_copyable T, typename Cursor,
          typename E = typename Cursor::value_type>
    requires (sizeof(T) % sizeof(E) == 0)
constexpr std::size_t store_load(T &v, Cursor &r) {
    constexpr auto N = sizeof(T) / sizeof(E);

    // copy + bit_cast instead of reinterpret_cast to stay constexpr;
    // gcc optimizes this down to a single memory load/store.
    if constexpr (D == fmt_direction::parse_value) {
        std::array<E, N> buf;
        r.read(buf.data(), N);
        v = std::bit_cast<T>(buf);
    } else {
        auto buf = std::bit_cast<std::array<E, N>>(v);
        r.write(buf.data(), N);
    }
    return N;
}

template <fmt_direction D, std::ranges::input_range TRg, typename Cursor,
          typename TElem = std::ranges::range_value_t<TRg>,
          typename E     = Cursor::value_type>
    requires (trivially_copyable<TElem> and sizeof(TElem) % sizeof(E) == 0)
constexpr std::size_t store_load_range(TRg &&vals, Cursor &r) {
    auto start = r.position();
    for (auto &v : subrange_of<D == fmt_direction::format_value>(vals))
        store_load<D>(v, r);
    return r.position() - start;
}

template <auto P, primitive<P> T>
struct [[=priority::impl(0)]] primitive_formatter : base_primitive_formatter<P, T> {
    template <typename Ctx>
        requires (parsable_into<Ctx> and
                  sizeof(T) % sizeof(typename Ctx::buffer_type::value_type) == 0)
    static constexpr void fmt(Ctx ctx) {
        store_load<Ctx::direction>(ctx.value, ctx.r);
    }
};

template <auto P, std::ranges::range T>
struct [[=priority::impl(1)]] range_formatter : base_range_formatter<P, T> {
    using E = std::ranges::range_value_t<T>;

    template <typename Ctx>
        requires parsable_into<Ctx>
    static constexpr void fmt(Ctx ctx) {
        using EF = [: Ctx::root::formatter_for(^^E) :];

        // numbers stored as in memory need no formatter each
        if constexpr (requires { requires EF::in_memory; } and
                      requires { store_load_range<Ctx::direction>(ctx.value, ctx.r); }) {
            store_load_range<Ctx::direction>(ctx.value, ctx.r);
        } else {
            for (auto &e : ctx.value)
                ctx.fmt(e);
        }
    }
};

template <typename Root, fmt_direction D, typename T, typename Rg>
using context_for = formatter_context<Root, D, T, info{}, cursor_for<Rg &>>;

template <typename Root, typename T, typename Rg>
concept parser_for =
    input_buffer<Rg &> and Root::check_type(^^T) and
    requires(context_for<Root, fmt_direction::parse_value, T, Rg> p) { Root::fmt(p); };

template <typename Root, typename T, typename Rg>
concept formatter_into =
    output_buffer<Rg &> and Root::check_type(^^T) and
    requires(context_for<Root, fmt_direction::format_value, const T, Rg> f) { Root::fmt(f); };

} // namespace detail::binary_fmt

// the built-in formatters, to derive user formatters from.
// a formatter deriving from one is of mid priority unless annotated otherwise
template <auto P, typename T>
using range_formatter = detail::binary_fmt::range_formatter<P, T>;

template <auto P, typename T>
using primitive_formatter = detail::binary_fmt::primitive_formatter<P, T>;

template <auto P, typename T>
using class_formatter = detail::base_class_formatter<P, T>;

template <auto P, template <auto, typename> typename ...UserFmts>
struct binfmt {
    using root = detail::formatter<P,
        UserFmts...,
        detail::binary_fmt::range_formatter,
        detail::binary_fmt::primitive_formatter,
        detail::base_class_formatter>;

    static consteval bool check_type(std::meta::info m) {
        return root::check_type(m);
    }

    template <typename T, typename Rg>
        requires detail::binary_fmt::parser_for<root, T, std::remove_reference_t<Rg>>
    static constexpr std::size_t parse(T &v, Rg &&r) {
        return root::template fmt<fmt_direction::parse_value>(v, r);
    }

    template <typename T, typename Rg>
        requires detail::binary_fmt::formatter_into<root, T, std::remove_reference_t<Rg>>
    static constexpr std::size_t format(const T &v, Rg &&r) {
        return root::template fmt<fmt_direction::format_value>(v, r);
    }
};

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
