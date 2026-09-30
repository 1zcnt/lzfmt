#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <meta>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace lz::fmt {

enum class fmt_direction { parse_value, format_value };

struct params {
    std::endian endianness       = std::endian::native;
    bool        enum_is_integral = false;
};

struct priority {
    int value = 0;

    constexpr priority operator()(int offset) const { return {value + offset}; }
    constexpr auto operator<=>(const priority &) const = default;

    static const priority high, mid, low, impl;
};

inline constexpr priority priority::high  {std::numeric_limits<int>::min() / 2};
inline constexpr priority priority::mid   {0};
inline constexpr priority priority::low   {9999};
inline constexpr priority priority::impl  {9999999};

namespace detail {

using namespace std::meta;
inline constexpr auto mctx = access_context::unchecked();

consteval info clean(info t) {
    return dealias(remove_cvref(t));
}

template <std::ranges::input_range R>
consteval auto static_array(R &&r) {
    return std::define_static_array(std::forward<R>(r));
}

consteval info field_named(info t, std::string_view name) {
    for (auto m : nonstatic_data_members_of(t, mctx))
        if (has_identifier(m) and identifier_of(m) == name)
            return m;
    return {};
}
} // namespace detail

// FIXME: redesign this
// the library's params, read by name from any P; fields P lacks keep their defaults
template <auto P>
inline constexpr params params_of = [] {
    params r;
    template for (constexpr auto m : std::define_static_array(nonstatic_data_members_of(^^params, detail::mctx))) {
        constexpr auto f = detail::field_named(detail::clean(^^decltype(P)), std::meta::identifier_of(m));
        if constexpr (f != std::meta::info{})
            r.[:m:] = P.[:f:];
    }
    return r;
}();

template <auto M>
struct member_id { };

enum class visit {
    next,
    step_in,
    step_up,
    stop,
};

namespace detail {
struct no_callback {
    constexpr no_callback operator()(auto) const { return {}; }
};


// formatter


template <std::input_or_output_iterator It, std::sentinel_for<It> S, typename E>
class cursor {
    // stepping past an element of a one-pass stream may block waiting for the
    // next one: step only when needed, so a value can be the stream's last
    static constexpr bool lazy = std::input_iterator<It> and not std::forward_iterator<It>;
    static constexpr bool sized = std::forward_iterator<It> and std::sized_sentinel_for<S, It>;

    It          it_;
    S           end_;
    std::size_t pos_     = 0;
    bool        pending_ = false;   // lazy: it_ is at the element read last

    std::span<const E> ahead_;

    constexpr bool has_ahead() const {
        return !ahead_.empty();
    }

    constexpr void settle() {
        if constexpr (lazy)
            if (std::exchange(pending_, false))
                ++it_;
    }

    constexpr void require(std::size_t n) const {
        if constexpr (sized)
            if (std::cmp_less(end_ - it_, n))
                throw std::length_error("EOF");
    }

public:
    using value_type = E;

    static constexpr bool contiguous = std::contiguous_iterator<It> and std::sized_sentinel_for<S, It>;

    constexpr cursor(It it, S end) : it_(std::move(it)), end_(std::move(end)) { }

    [[nodiscard]] constexpr auto rest() const requires contiguous {
        return std::span(std::to_address(it_), static_cast<std::size_t>(end_ - it_));
    }

    constexpr void skip(std::size_t n) requires sized {
        require(n);
        it_ += static_cast<std::iter_difference_t<It>>(n);
        pos_ += n;
    }

    [[nodiscard]] constexpr std::size_t position() const { return pos_; }

    [[nodiscard]] constexpr bool at_end() {
        if (has_ahead())
            return false;
        settle();
        return it_ == end_;
    }

    constexpr E get() {
        if (has_ahead()) {
            E v = ahead_[0];
            ahead_ = ahead_.subspan(1);
            ++pos_;
            return v;
        }

        if (at_end())
            throw std::length_error("EOF");
        E v = *it_;
        if constexpr (lazy) pending_ = true;
        else                ++it_;
        ++pos_;
        return v;
    }

    constexpr void put(const E &v) {
        if (it_ == end_)
            throw std::length_error("EOF");
        *it_ = v;
        ++it_;
        ++pos_;
    }

    constexpr void read(E *out, std::size_t n) {
        if (has_ahead()) {
            for (std::size_t i = 0; i < n; ++i)
                out[i] = get();
        } else if constexpr (sized) {
            require(n);
            it_ = std::ranges::copy_n(std::move(it_), static_cast<std::iter_difference_t<It>>(n), out).in;
            pos_ += n;
        } else {
            for (std::size_t i = 0; i < n; ++i)
                out[i] = get();
        }
    }

    constexpr std::size_t read_some(E *out, std::size_t n) {
        if constexpr (sized) {
            if (!has_ahead()) {
                n = std::min(n, static_cast<std::size_t>(end_ - it_));
                read(out, n);
                return n;
            }
        }
        std::size_t i = 0;
        for (; i < n and !at_end(); ++i)
            out[i] = get();
        return i;
    }

    constexpr void set_ahead(std::span<const E> last) {
        if (has_ahead())
            throw std::runtime_error("set_ahead: cursor still has ahead buffer");
        ahead_ = last;
        pos_ -= last.size();
    }

    constexpr void write(const E *in, std::size_t n) {
        if constexpr (sized) {
            require(n);
            it_ = std::ranges::copy_n(in, static_cast<std::iter_difference_t<It>>(n), std::move(it_)).out;
            pos_ += n;
        } else {
            for (std::size_t i = 0; i < n; ++i)
                put(in[i]);
        }
    }
};

// output-only ranges have no value type
template <typename Rg>
struct buffer_element { using type = std::byte; };
template <typename Rg>
    requires requires { typename std::ranges::range_value_t<Rg>; }
struct buffer_element<Rg> { using type = std::ranges::range_value_t<Rg>; };

template <typename Rg>
using buffer_element_t = buffer_element<std::remove_reference_t<Rg>>::type;

template <typename Rg>
using cursor_for = cursor<std::ranges::iterator_t<Rg>, std::ranges::sentinel_t<Rg>, buffer_element_t<Rg>>;

template <typename Rg>
constexpr auto make_cursor(Rg &r) {
    return cursor_for<Rg &>{std::ranges::begin(r), std::ranges::end(r)};
}

template <typename Rg>
    requires std::ranges::contiguous_range<std::ranges::range_value_t<Rg>>
constexpr auto make_cursor(Rg &r) {
    auto view = r | std::views::join;
    return cursor_for<Rg &>{std::ranges::begin(view), std::ranges::end(view)};
}

template <typename F>
struct storage_entry {
    F::storage_t value{};
};

template <typename F>
inline constexpr bool has_storage = false;
template <typename F> requires (not std::is_void_v<typename F::storage_t>)
inline constexpr bool has_storage<F> = true;

template <
    typename Root,
    fmt_direction D,
    typename T,
    info Subject,
    typename Cursor,
    typename ...Entries
>
struct formatter_context {
    using root        = Root;
    using value_type  = T;
    using buffer_type = Cursor;

    static constexpr fmt_direction      direction = D;
    static constexpr info               type      = clean(^^T);
    static constexpr info               subject   = Subject;

    using mutable_type = formatter_context<Root, D, std::remove_cv_t<T>, Subject, Cursor, Entries...>;
    using const_type   = formatter_context<Root, D, const T, Subject, Cursor, Entries...>;

    T                       &value;
    Cursor                  &r;
    visit                   &action;
    std::tuple<Entries...>  &storage;

    template <info S = Subject, typename U>
    constexpr auto with(U &v) const {
        return with<S>(v, action);
    }

    template <info S = Subject, typename U>
    constexpr auto with(U &v, visit &a) const {
        return formatter_context<Root, D, U, S, Cursor, Entries...>{v, r, a, storage};
    }

    template <typename U>
    constexpr void fmt(U &v) const {
        if (action == visit::stop)
            return;

        visit a = visit::step_in;
        root::fmt(with<info{}>(v, a));
        if (a == visit::stop)
            action = visit::stop;
    }

    template <typename F>
    constexpr auto &get_storage() const {
        return std::get<storage_entry<F>>(storage).value;
    }
};

template <typename Root, fmt_direction D, typename T, typename Cursor, typename ...Entries>
constexpr auto make_context(T &v, Cursor &r, visit &a, std::tuple<Entries...> &s) {
    return formatter_context<Root, D, T, info{}, Cursor, Entries...>{v, r, a, s};
}

template <bool Const, typename Rg>
constexpr auto subrange_of(Rg &r) {
    if constexpr (Const) return std::ranges::subrange(std::ranges::cbegin(r), std::ranges::cend(r));
    else                 return std::ranges::subrange(r);
}

// a base's priority is inherited, except the built-in ones' impl:
// a user formatter deriving from a built-in one is mid by default
consteval priority priority_of(info t) {
    if (auto a = annotations_of_with_type(t, ^^priority); !a.empty())
        return extract<priority>(a[0]);
    for (auto b : bases_of(t, mctx))
        if (auto p = priority_of(type_of(b)); p != priority::mid and p < priority::impl)
            return p;
    return priority::mid;
}

} // namespace detail

template <typename Ctx>
concept parsable_into = Ctx::direction == fmt_direction::format_value or
                        not std::is_const_v<typename Ctx::value_type>;

// ctx.value can be changed: parsing, or formatting a copy
template <typename Ctx>
concept mutable_value = not std::is_const_v<typename Ctx::value_type>;

template <typename Ctx, typename Parse, typename Format = detail::no_callback>
constexpr decltype(auto) direction_split(Ctx ctx, Parse &&parse, Format &&format = {}) {
    if constexpr (Ctx::direction == fmt_direction::parse_value) return std::forward<Parse>(parse)(ctx);
    else                                                        return std::forward<Format>(format)(ctx);
}

namespace detail {
template <typename R>
concept overridden = not std::same_as<R, no_callback>;

template <typename F, typename Ctx>
concept pre_fmt_takes_const = requires (typename Ctx::const_type c) { F::pre_fmt(c); };

// a formatter writing a value it changes first works on a copy of it:
// one overriding value_side, or whose pre_fmt only takes a mutable value
template <typename F, typename Ctx>
concept formats_from_copy =
    Ctx::direction == fmt_direction::format_value and
    requires (typename Ctx::mutable_type c) {
        requires overridden<decltype(F::value_side(c))> or
                 (overridden<decltype(F::pre_fmt(c))> and not pre_fmt_takes_const<F, Ctx>);
    };
}

template <auto P, typename T, template <auto, typename> typename Derived>
struct base_formatter {
    static consteval std::vector<std::meta::info> members()  { return {}; }
    static consteval std::vector<std::meta::info> required() { return {}; }

    using storage_t = void;

    static constexpr void fmt(auto) { }

    // no_callback tells the root the hook isn't overridden
    static constexpr detail::no_callback pre_fmt(auto)     { return {}; }
    static constexpr detail::no_callback post_fmt(auto)    { return {}; }
    static constexpr detail::no_callback value_side(auto)  { return {}; }
    static constexpr detail::no_callback buffer_side(auto) { return {}; }
};

namespace detail {

template <typename T>
concept numeric = std::integral<T> or std::floating_point<T>;

template <typename T, auto P>
concept primitive =
    (std::same_as<T, std::byte> or numeric<T> or
     (std::is_enum_v<T> and params_of<P>.enum_is_integral)) and
    (sizeof(T) == 1 or sizeof(T) == 2 or sizeof(T) == 4 or sizeof(T) == 8);

consteval bool has_bit_fields(info t) {
    return std::ranges::any_of(nonstatic_data_members_of(t, mctx), is_bit_field);
}

template <typename T>
concept member_wise = std::is_class_v<T> and not has_bit_fields(^^T);

template <auto P, primitive<P> T>
struct [[=priority::impl(0)]] base_primitive_formatter : base_formatter<P, T, base_primitive_formatter> {
    template <std::size_t N> using uint_of_size =
        std::conditional_t<N == 1, std::uint8_t,
        std::conditional_t<N == 2, std::uint16_t,
        std::conditional_t<N == 4, std::uint32_t,
        std::conditional_t<N == 8, std::uint64_t, void>>>>;

    static constexpr bool in_memory = params_of<P>.endianness == std::endian::native or sizeof(T) == 1;

    template <typename Ctx>
    static constexpr void value_side(Ctx ctx) {
        if constexpr (not in_memory) {
            using U = uint_of_size<sizeof(T)>;
            ctx.value = std::bit_cast<T>(std::byteswap(std::bit_cast<U>(ctx.value)));
        }
    }
};

template <auto P, std::ranges::range T>
struct [[=priority::impl(1)]] base_range_formatter : base_formatter<P, T, base_range_formatter> {
    using E = std::ranges::range_value_t<T>;

    static consteval std::vector<info> required() { return {^^E}; }
};

template <auto P, member_wise T>
struct [[=priority::impl(3)]] base_class_formatter : base_formatter<P, T, base_class_formatter> {
    static consteval std::vector<info> members() {
        return nonstatic_data_members_of(^^T, mctx);
    }

    template <info M>
    static constexpr auto &access_member(T &v) {
        return v.[:M:];
    }

    template <info M>
    static constexpr auto &access_member(const T &v) {
        return v.[:M:];
    }
};

template <typename F>
inline constexpr auto fmt_members = static_array(F::members());

template <typename F>
inline constexpr auto fmt_required = static_array(F::required());

// spans aren't structural: extracted as pointer and size
// FIXME: structural span
template <typename F> inline constexpr const info *fmt_members_data  = fmt_members<F>.data();
template <typename F> inline constexpr std::size_t fmt_members_size  = fmt_members<F>.size();
template <typename F> inline constexpr const info *fmt_required_data = fmt_required<F>.data();
template <typename F> inline constexpr std::size_t fmt_required_size = fmt_required<F>.size();

template <typename F, info M, typename V>
inline constexpr info member_value_type =
    remove_cvref(^^decltype(F::template access_member<M>(std::declval<V &>())));

consteval info member_type_of(info f, info m, info v) {
    return extract<info>(substitute(^^member_value_type, {f, reflect_constant(m), v}));
}

consteval std::vector<info> members_of_fmt(info f) {
    auto data = extract<const info *>(substitute(^^fmt_members_data, {f}));
    return {data, data + extract<std::size_t>(substitute(^^fmt_members_size, {f}))};
}
consteval std::vector<info> required_of_fmt(info f) {
    auto data = extract<const info *>(substitute(^^fmt_required_data, {f}));
    return {data, data + extract<std::size_t>(substitute(^^fmt_required_size, {f}))};
}

template <auto P, template <auto, typename> typename ...Fmts>
struct formatter {
    static constexpr auto fmt_templates = static_array(std::vector{^^Fmts...});

private:
    // types in `visiting` are assumed formattable, so recursive types are
    static consteval info find(info t, std::vector<info> visiting) {
        t = clean(t);

        std::vector<std::pair<std::pair<priority, std::size_t>, info>> candidates;
        for (std::size_t i = 0; i < fmt_templates.size(); ++i) {
            std::vector args{reflect_constant(P), t};
            if (can_substitute(fmt_templates[i], args)) {
                auto f = substitute(fmt_templates[i], args);
                candidates.push_back({{priority_of(f), i}, f});
            }
        }
        std::ranges::sort(candidates, {}, [](auto &c) { return c.first; });

        visiting.push_back(t);
        for (auto &[_, f] : candidates)
            if (usable(f, t, visiting))
                return f;
        return {};
    }

    static consteval bool usable(info f, info t, const std::vector<info> &visiting) {
        auto needs = required_of_fmt(f);
        for (auto m : members_of_fmt(f))
            needs.push_back(member_type_of(f, m, t));
        for (auto n : needs)
            if (!std::ranges::contains(visiting, clean(n)) and find(n, visiting) == info{})
                return false;
        return true;
    }

public:
    static consteval info formatter_for(info t) {
        return find(t, {});
    }

private:
    template <typename Ctx>
    using fmt_of = [: formatter_for(Ctx::type) :];

    template <typename Ctx>
    static consteval bool dispatchable() { return requires(Ctx c) { fmt_of<Ctx>::fmt(c); }; }

public:
    static consteval bool check_type(info m) { return formatter_for(m) != info{}; }

    template <typename Ctx>
        requires (formatter_for(Ctx::type) != info{} and parsable_into<Ctx> and dispatchable<Ctx>())
    static constexpr void fmt(Ctx ctx) {
        using F = fmt_of<Ctx>;

        if constexpr (formats_from_copy<F, Ctx>) {
            std::remove_cv_t<typename Ctx::value_type> copy = ctx.value;
            fmt_sides<F>(ctx.with(copy));
        } else
            fmt_sides<F>(ctx);
    }

private:
    template <typename F, typename Ctx>
    static constexpr void fmt_sides(Ctx ctx) {
        direction_split(ctx,
            [](auto c) { F::buffer_side(c); if (fmt_value<F>(c)) F::value_side(c); },
            [](auto c) { F::value_side(c);  if (fmt_value<F>(c)) F::buffer_side(c); });
    }

    // parsed value's members come first, a formatted value's last.
    // false if ctx.action cut the value short
    template <typename F, typename Ctx>
    static constexpr bool fmt_value(Ctx ctx) {
        F::pre_fmt(ctx);
        bool done = direction_split(ctx,
            [](auto c) { return fmt_each_member<F>(c) and fmt_self<F>(c); },
            [](auto c) { return fmt_self<F>(c) and fmt_each_member<F>(c); });
        if (done)
            F::post_fmt(ctx);
        return done;
    }

    template <typename F, typename Ctx>
    static constexpr bool fmt_self(Ctx ctx) {
        F::fmt(ctx);
        return ctx.action != visit::stop and ctx.action != visit::step_up;
    }

    template <typename F, typename Ctx>
    static constexpr bool fmt_each_member(Ctx ctx) {
        if (ctx.action != visit::step_in)
            return ctx.action == visit::next;

        template for (constexpr auto m : fmt_members<F>) {
            visit a = visit::step_in;
            fmt(ctx.template with<m>(F::template access_member<m>(ctx.value), a));
            if (a == visit::stop) {
                ctx.action = visit::stop;
                return false;
            }
            if (a == visit::step_up)
                break;
        }
        return true;
    }

    static consteval void collect_formatters(info t, std::vector<info> &out, std::vector<info> &seen) {
        t = clean(t);
        if (std::ranges::contains(seen, t))
            return;
        seen.push_back(t);

        auto f = formatter_for(t);
        if (f == info{})
            return;
        if (!std::ranges::contains(out, f))
            out.push_back(f);
        for (auto n : required_of_fmt(f))
            collect_formatters(n, out, seen);
        for (auto m : members_of_fmt(f))
            collect_formatters(member_type_of(f, m, t), out, seen);
    }

    static consteval std::vector<info> storage_entries(info t) {
        std::vector<info> fmts, seen, entries;
        collect_formatters(t, fmts, seen);
        for (auto f : fmts)
            if (extract<bool>(substitute(^^has_storage, {f})))
                entries.push_back(substitute(^^storage_entry, {f}));
        return entries;
    }

public:
    template <typename T>
    using storage_for = [: substitute(^^std::tuple, storage_entries(^^T)) :];

    template <fmt_direction D, typename T, typename Rg>
    static constexpr size_t fmt(T &v, Rg &r) {
        storage_for<T> storage;
        auto cur    = detail::make_cursor(r);
        auto action = visit::step_in;
        fmt(detail::make_context<formatter, D>(v, cur, action, storage));
        return cur.position();
    }
};

} // namespace detail
} // namespace lz::fmt
