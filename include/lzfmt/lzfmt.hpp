#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ios>
#include <iterator>
#include <limits>
#include <meta>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace lz::fmt {

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
struct no_callback { };


// formatter


// what a cursor needs of std::basic_streambuf, so other types can stand in for it
template <typename Sb>
concept input_streambuf = requires(Sb &sb, typename Sb::char_type *p, std::streamsize n) {
    { sb.sgetn(p, n) } -> std::convertible_to<std::streamsize>;
};

template <typename Sb>
concept output_streambuf = requires(Sb &sb, const typename Sb::char_type *p, std::streamsize n) {
    { sb.sputn(p, n) } -> std::convertible_to<std::streamsize>;
};

// a stream is used through its buffer: rdbuf() gives a pointer to it
template <typename S>
concept input_stream = requires(S s) {
    requires input_streambuf<std::remove_reference_t<decltype(*s.rdbuf())>>;
};

template <typename S>
concept output_stream = requires(S s) {
    requires output_streambuf<std::remove_reference_t<decltype(*s.rdbuf())>>;
};

template <typename Sb>
concept streambuf_like = input_streambuf<Sb> or output_streambuf<Sb>;

template <streambuf_like Sb>
class cursor {
    Sb         &sb_;
    std::size_t pos_ = 0;

public:
    using value_type = Sb::char_type;

    constexpr explicit cursor(Sb &sb) : sb_(sb) { }

    [[nodiscard]] constexpr std::size_t position() const { return pos_; }

    constexpr std::size_t read_some(value_type *out, std::size_t n) {
        auto got = static_cast<std::size_t>(sb_.sgetn(out, static_cast<std::streamsize>(n)));
        pos_ += got;
        return got;
    }

    constexpr void read(value_type *out, std::size_t n) {
        if (read_some(out, n) != n)
            throw std::length_error("EOF");
    }

    constexpr void write(const value_type *in, std::size_t n) {
        auto put = static_cast<std::size_t>(sb_.sputn(in, static_cast<std::streamsize>(n)));
        pos_ += put;
        if (put != n)
            throw std::length_error("EOF");
    }

    constexpr value_type get() {
        value_type v{};
        read(&v, 1);
        return v;
    }

    constexpr void put(const value_type &v) {
        write(&v, 1);
    }
};

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
    typename T,
    info Subject,
    typename Cursor,
    typename ...Entries
>
struct formatter_context {
    using root        = Root;
    using value_type  = T;
    using buffer_type = Cursor;

    static constexpr info               type      = clean(^^T);
    static constexpr info               subject   = Subject;

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
        return formatter_context<Root, U, S, Cursor, Entries...>{v, r, a, storage};
    }

    template <typename U>
    constexpr void read(U &v) const {
        nested(v, [](auto c) { root::read(c); });
    }

    template <typename U>
    constexpr void write(const U &v) const {
        nested(v, [](auto c) { root::write(c); });
    }

    // v is a value of its own: of what its formatters ask for, only stop gets out
    template <typename U, typename Fmt>
    constexpr void nested(U &v, Fmt fmt) const {
        if (action == visit::stop)
            return;

        visit a = visit::step_in;
        fmt(with<info{}>(v, a));
        if (a == visit::stop)
            action = visit::stop;
    }

    template <typename F>
    constexpr auto &get_storage() const {
        return std::get<storage_entry<F>>(storage).value;
    }
};

template <typename Root, typename T, typename Cursor, typename ...Entries>
constexpr auto make_context(T &v, Cursor &r, visit &a, std::tuple<Entries...> &s) {
    return formatter_context<Root, T, info{}, Cursor, Entries...>{v, r, a, s};
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

template <typename R>
concept overridden = not std::same_as<R, no_callback>;

}

template <typename Ctx>
struct hook { };

#define TRY_HOOK(Ctx, Fmt, Fn) \
    std::conditional_t<requires { hook<Ctx>::Fn; }, hook<Ctx>, Fmt>::Fn

template <auto P, typename T, template <auto, typename> typename Derived>
struct base_formatter {
    static consteval std::vector<std::meta::info> members()  { return {}; }
    static consteval std::vector<std::meta::info> required() { return {}; }

    using storage_t = void;

    // the value's own part: read(T &value, ctx) and write(const T &value, ctx).
    // its members are read before it and written after it
    static constexpr void read(auto &, auto) { }
    static constexpr void write(const auto &, auto) { }

    // no_callback tells the root the hook isn't overridden
    // on_value is called before write and after read
    // on_stream is the same but vice-versa
    // a formatter overriding on_value writes a copy of a const value, changed by it
    static constexpr detail::no_callback on_value(auto)     { return {}; }
    static constexpr detail::no_callback on_stream(auto)    { return {}; }
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
    static constexpr void on_value(Ctx ctx) {
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
    static consteval bool readable() {
        return not std::is_const_v<typename Ctx::value_type> and
               requires(Ctx c) { fmt_of<Ctx>::read(c.value, c); };
    }

    template <typename Ctx>
    static consteval bool writable() {
        return requires(Ctx c) { fmt_of<Ctx>::write(std::as_const(c.value), c); };
    }

public:
    static consteval bool check_type(info m) { return formatter_for(m) != info{}; }

    template <typename Ctx>
        requires (formatter_for(Ctx::type) != info{} and readable<Ctx>())
    static constexpr void read(Ctx ctx) {
        using F = fmt_of<Ctx>;

        TRY_HOOK(Ctx, F, on_stream(ctx));

        if (!each_member<F>(ctx, [](auto c) { read(c); }))
            return;

        TRY_HOOK(Ctx, F, read(ctx.value, ctx));
        if (proceeds(ctx))
            TRY_HOOK(Ctx, F, on_value(ctx));
    }

    template <typename Ctx>
        requires (formatter_for(Ctx::type) != info{} and writable<Ctx>())
    static constexpr void write(Ctx ctx) {
        using F = fmt_of<Ctx>;
        using T = [: Ctx::type :];

        if constexpr (std::is_const_v<typename Ctx::value_type> and
                      overridden<decltype(TRY_HOOK(Ctx, F, on_value(ctx.with(std::declval<T &>()))))>) {
            T copy = ctx.value;
            write_value<F>(ctx.with(copy));
        } else
            write_value<F>(ctx);
    }

private:
    template <typename F, typename Ctx>
    static constexpr void write_value(Ctx ctx) {
        TRY_HOOK(Ctx, F, on_value(ctx));
        TRY_HOOK(Ctx, F, write(std::as_const(ctx.value), ctx));
        if (proceeds(ctx) and each_member<F>(ctx, [](auto c) { write(c); }))
            TRY_HOOK(Ctx, F, on_stream(ctx));
    }

    template <typename Ctx>
    static constexpr bool proceeds(Ctx ctx) {
        return ctx.action != visit::stop and ctx.action != visit::step_up;
    }

    // false if ctx.action cut the value short
    template <typename F, typename Ctx, typename Fmt>
    static constexpr bool each_member(Ctx ctx, [[maybe_unused]] Fmt fmt) {
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

private:
    template <typename T, typename Sb>
    using context_for = decltype(make_context<formatter>(
        std::declval<T &>(), std::declval<cursor<Sb> &>(),
        std::declval<visit &>(), std::declval<storage_for<T> &>()));

public:
    template <typename T, input_stream S>
    static constexpr std::size_t read(T &v, S &&s) {
        return read(v, *s.rdbuf());
    }

    template <typename T, output_stream S>
    static constexpr std::size_t write(const T &v, S &&s) {
        return write(v, *s.rdbuf());
    }

    // sb is a std::streambuf, or anything else with its char_type, sgetn() and sputn()
    template <typename T, typename Sb, typename B = std::remove_reference_t<Sb>>
        requires input_streambuf<B> and (check_type(^^T)) and
                 requires(context_for<T, B> c) { read(c); }
    static constexpr std::size_t read(T &v, Sb &&sb) {
        return run(v, sb, [](auto c) { read(c); });
    }

    template <typename T, typename Sb, typename B = std::remove_reference_t<Sb>>
        requires output_streambuf<B> and (check_type(^^T)) and
                 requires(context_for<const T, B> c) { write(c); }
    static constexpr std::size_t write(const T &v, Sb &&sb) {
        return run(v, sb, [](auto c) { write(c); });
    }

private:
    template <typename T, typename Sb, typename Fmt>
    static constexpr std::size_t run(T &v, Sb &sb, Fmt fmt) {
        storage_for<T> storage;
        cursor cur{sb};
        auto action = visit::step_in;
        fmt(make_context<formatter>(v, cur, action, storage));
        return cur.position();
    }
};

} // namespace detail
} // namespace lz::fmt
