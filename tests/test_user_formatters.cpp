#include <iostream>
#include <print>

#include "check.hpp"

#include <string>

using lz::fmt::fmt_direction;
using lz::fmt::visit;

template <typename T> constexpr bool is_vector = false;
template <typename E> constexpr bool is_vector<std::vector<E>> = true;

// a vector, prefixed by its size as a uint16
template <auto P, typename T> requires is_vector<T>
struct sized_vector_formatter : lz::fmt::base_formatter<P, T, sized_vector_formatter> {
    template <typename Ctx>
    static constexpr void fmt(Ctx ctx) {
        auto n = static_cast<std::uint16_t>(ctx.value.size());
        ctx.fmt(n);
        lz::fmt::direction_split(ctx, [n](auto c) { c.value.resize(n); });
        for (auto &e : ctx.value)
            ctx.fmt(e);
    }
};

using sized = lz::fmt::binfmt<params{}, sized_vector_formatter>;

struct message {
    std::uint8_t              kind;
    std::vector<std::uint8_t> body;
};

// a user formatter takes over the types it accepts, anywhere they appear
static void overrides_builtin() {
    std::vector<std::uint8_t> v{7, 8};
    std::array<std::uint8_t, 4> out{};
    CHECK(sized::format(v, out) == 4 and out == bytes(2, 0, 7, 8));

    std::vector<std::uint8_t> back;
    CHECK(sized::parse(back, out) == 4 and back == v);

    message m;
    CHECK(sized::parse(m, bytes(1, 3, 0, 4, 5, 6)) == 6);
    CHECK(m.kind == 1 and m.body == std::vector<std::uint8_t>{4, 5, 6});
}

// other types still use the builtin formatters
static void leaves_other_types() {
    std::array<std::uint8_t, 2> arr;
    CHECK(sized::parse(arr, bytes(1, 2)) == 2 and arr == bytes(1, 2));
}

std::string trace;

struct traced {
    std::uint8_t x;
};

// logs the order it's called in; value_side sees the member, buffer_side the cursor
template <auto P, typename T> requires std::same_as<T, traced>
struct traced_formatter : lz::fmt::base_formatter<P, T, traced_formatter> {
    static consteval std::vector<std::meta::info> members() { return {^^traced::x}; }

    template <std::meta::info M> static constexpr auto &access_member(T &v)       { return v.[:M:]; }
    template <std::meta::info M> static constexpr auto &access_member(const T &v) { return v.[:M:]; }

    template <typename Ctx> static constexpr void pre_fmt(Ctx)          { trace += "pre "; }
    template <typename Ctx> static constexpr void fmt(Ctx ctx)          { trace += "fmt:" + std::to_string(ctx.value.x) + " "; }
    template <typename Ctx> static constexpr void post_fmt(Ctx ctx)     { trace += "post:" + std::to_string(ctx.value.x) + " "; }
    template <typename Ctx> static constexpr void value_side(Ctx ctx)  { trace += "value:" + std::to_string(ctx.value.x) + " "; }
    template <typename Ctx> static constexpr void buffer_side(Ctx ctx) { trace += "buffer:" + std::to_string(ctx.r.position()) + " "; }
};

using tracing = lz::fmt::binfmt<params{}, traced_formatter>;

// parsing: buffer_side, pre_fmt, the members, fmt, post_fmt, then value_side
static void side_order_parse() {
    traced t{};
    trace.clear();
    CHECK(tracing::parse(t, bytes(7)) == 1);
    CHECK(trace == "buffer:0 pre fmt:7 post:7 value:7 ");
}

// formatting: value_side, pre_fmt, fmt, the members, post_fmt, then buffer_side
static void side_order_format() {
    std::array<std::uint8_t, 1> out{};
    trace.clear();
    CHECK(tracing::format(traced{7}, out) == 1);
    CHECK(trace == "value:7 pre fmt:7 post:7 buffer:1 ");
}

struct inverted {
    std::uint8_t x;
};

// stored with its bits flipped
template <auto P, std::same_as<inverted> T>
struct inverted_formatter : lz::fmt::class_formatter<P, T> {
    template <typename Ctx>
    static constexpr void value_side(Ctx ctx) { ctx.value.x = static_cast<std::uint8_t>(~ctx.value.x); }
};

using inverting = lz::fmt::binfmt<params{}, inverted_formatter>;

// the members are formatted from value_side's copy, and the value is left alone
static void value_side_converts_members() {
    const inverted v{0x0F};
    std::array<std::uint8_t, 1> out{};
    CHECK(inverting::format(v, out) == 1 and out == bytes(0xF0) and v.x == 0x0F);

    inverted back{};
    CHECK(inverting::parse(back, out) == 1 and back.x == 0x0F);
}

struct summed {
    std::uint8_t a;
    std::uint8_t b;
    std::uint8_t sum;
};

// fills in sum before it's written
template <auto P, std::same_as<summed> T>
struct summed_formatter : lz::fmt::class_formatter<P, T> {
    template <typename Ctx> requires lz::fmt::mutable_value<Ctx>
    static constexpr void pre_fmt(Ctx ctx) {
        lz::fmt::direction_split(ctx, [](auto) { }, [](auto c) {
            c.value.sum = static_cast<std::uint8_t>(c.value.a + c.value.b);
        });
    }
};

// a pre_fmt taking only a mutable value gets a copy when formatting
static void pre_fmt_on_copy() {
    const summed v{1, 2, 0};
    std::array<std::uint8_t, 3> out{};
    CHECK(lz::fmt::binfmt<params{}, summed_formatter>::format(v, out) == 3);
    CHECK(out == bytes(1, 2, 3) and v.sum == 0);
}

int copies = 0;

struct counted : std::vector<std::uint8_t> {
    using vector::vector;
    counted(const counted &o) : vector(o) { ++copies; }
    counted &operator=(const counted &) = default;
};

// a range prefixed by its size as a uint16
template <auto P, std::same_as<counted> T>
struct prefixed_len_formatter : lz::fmt::range_formatter<P, T> {
    template <typename Ctx>
    static constexpr void pre_fmt(Ctx ctx) {
        lz::fmt::direction_split(ctx,
            [](auto c) { std::uint16_t n; c.fmt(n); c.value.resize(n); },
            [](auto c) { auto n = static_cast<std::uint16_t>(c.value.size()); c.fmt(n); });
    }
};

using prefixed = lz::fmt::binfmt<params{}, prefixed_len_formatter>;

// a pre_fmt taking the const value formats it in place
static void pre_fmt_on_const() {
    const counted v{7, 8, 9};
    std::array<std::uint8_t, 5> out{};
    copies = 0;
    CHECK(prefixed::format(v, out) == 5 and out == bytes(3, 0, 7, 8, 9) and copies == 0);

    counted back;
    CHECK(prefixed::parse(back, out) == 5 and back == v);
}

// a formatter deriving from a built-in one isn't of the built-in's priority
static void derived_builtin_priority() {
    static_assert(lz::fmt::detail::priority_of(^^prefixed_len_formatter<params{}, counted>) ==
                  lz::fmt::priority::mid);
    static_assert(prefixed::root::formatter_for(^^counted) ==
                  ^^prefixed_len_formatter<params{}, counted>);
}

using net_formatters = lz::fmt::formatters<prefixed_len_formatter>;
using sum_formatters = lz::fmt::formatters<summed_formatter>;

// formatter lists declared apart are joined
static void joined_formatters() {
    using fmt = lz::fmt::binfmt_of<params{}, net_formatters, sum_formatters>;
    static_assert(std::same_as<fmt, lz::fmt::binfmt<params{}, prefixed_len_formatter, summed_formatter>>);
    static_assert(std::same_as<lz::fmt::binfmt_of<params{}>, lz::fmt::binfmt<params{}>>);

    std::array<std::uint8_t, 4> out{};
    CHECK(fmt::format(counted{1, 2}, out) == 4 and out == bytes(2, 0, 1, 2));
}

struct only_endianness {
    std::endian endianness = std::endian::big;
};

// params missing fields keep the library defaults
static void partial_params() {
    std::array<std::uint8_t, 2> out{};
    CHECK(lz::fmt::binfmt<only_endianness{}>::format(std::uint16_t{0x0102}, out) == 2 and out == bytes(1, 2));
    CHECK(not lz::fmt::params_of<only_endianness{}>.enum_is_integral);
}

struct opaque {
    std::uint8_t x;
    std::uint8_t y;
};

// a class whose members are skipped: its pre_fmt() sets next
template <auto P, std::same_as<opaque> T>
struct opaque_formatter : lz::fmt::class_formatter<P, T> {
    template <typename Ctx>
    static constexpr void pre_fmt(Ctx ctx) {
        ctx.action = visit::next;
    }
};

using opaques = lz::fmt::binfmt<params{}, opaque_formatter>;

struct opaque_then_point {
    opaque o;
    struct { std::uint8_t a, b; } p;
};

// next skips the members of that value only
static void next_skips_members() {
    opaque o{};
    CHECK(opaques::parse(o, bytes(1, 2)) == 0 and o.x == 0);

    opaque_then_point op{};
    CHECK(opaques::parse(op, bytes(1, 2)) == 2);

    CHECK(op.o.x == 0 and op.p.a == 1 and op.p.b == 2);
}

struct terminator {
    std::uint8_t v;
};

// a byte, ending the object it's in when zero
template <auto P, typename T> requires std::same_as<T, terminator>
struct terminator_formatter : lz::fmt::base_formatter<P, T, terminator_formatter> {
    template <typename Ctx>
    static constexpr void fmt(Ctx ctx) {
        ctx.fmt(ctx.value.v);
        if (ctx.value.v == 0)
            ctx.action = visit::step_up;
    }
};

using terminated = lz::fmt::binfmt<params{}, terminator_formatter>;

struct record {
    std::uint8_t a;
    terminator   t;
    std::uint8_t b;
};

// step_up skips the rest of the enclosing object only
static void step_up_skips_rest() {
    record r{};
    CHECK(terminated::parse(r, bytes(1, 1, 2)) == 3 and r.b == 2);

    r = {};
    CHECK(terminated::parse(r, bytes(1, 0, 2)) == 2 and r.a == 1 and r.b == 0);

    std::vector<record> v(2);
    CHECK(terminated::parse(v, bytes(1, 0, 2, 1, 3)) == 5);
    CHECK(v[0].a == 1 and v[0].b == 0 and v[1].a == 2 and v[1].b == 3);
}

int main() {
    overrides_builtin();
    leaves_other_types();
    side_order_parse();
    side_order_format();
    value_side_converts_members();
    pre_fmt_on_copy();
    pre_fmt_on_const();
    derived_builtin_priority();
    joined_formatters();
    partial_params();
    next_skips_members();
    step_up_skips_rest();
    return failures != 0;
}
