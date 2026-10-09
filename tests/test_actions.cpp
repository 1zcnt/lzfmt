#include "check.hpp"

using lz::fmt::visit;

struct opaque {
    std::uint8_t x;
};

template <auto P, std::same_as<opaque> T>
struct opaque_formatter : lz::fmt::class_formatter<P, T> {
    static constexpr void on_stream(auto ctx) { ctx.action = visit::next; }
};

struct terminator {
    std::uint8_t v;
};

template <auto P, std::same_as<terminator> T>
struct terminator_formatter : lz::fmt::base_formatter<P, T, terminator_formatter> {
    static constexpr void read(T &t, auto ctx) {
        ctx.read(t.v);
        if (t.v == 0)
            ctx.action = visit::step_up;
    }

    static constexpr void write(const T &t, auto ctx) {
        ctx.write(t.v);
        if (t.v == 0)
            ctx.action = visit::step_up;
    }
};

struct halt { };

template <auto P, std::same_as<halt> T>
struct halt_formatter : lz::fmt::base_formatter<P, T, halt_formatter> {
    static constexpr void read(T &, auto ctx)        { ctx.action = visit::stop; }
    static constexpr void write(const T &, auto ctx) { ctx.action = visit::stop; }
};

struct record {
    std::uint8_t a;
    terminator   t;
    std::uint8_t b;
};

// a record, formatted through ctx.read() and ctx.write() rather than as members
struct wrapper {
    record r;
};

template <auto P, std::same_as<wrapper> T>
struct wrapper_formatter : lz::fmt::base_formatter<P, T, wrapper_formatter> {
    static constexpr void read(T &w, auto ctx)        { ctx.read(w.r); }
    static constexpr void write(const T &w, auto ctx) { ctx.write(w.r); }
};

struct opaques_then_byte {
    std::vector<opaque> v;
    std::uint8_t        after;
};

struct record_then_byte {
    record       r;
    std::uint8_t after;
};

struct wrapper_then_byte {
    wrapper      w;
    std::uint8_t after;
};

struct halting {
    std::uint8_t a;
    halt         h;
    std::uint8_t b;
};

struct halting_then_byte {
    halting      in;
    std::uint8_t after;
};

struct halting_elem {
    std::uint8_t a;
    halt         h;
};

struct halting_elems_then_byte {
    std::vector<halting_elem> v;
    std::uint8_t              after;
};

using fmt = lz::fmt::binfmt<params{}, opaque_formatter, terminator_formatter, halt_formatter, wrapper_formatter>;

// next skips each element of a range separately
static void next_in_range() {
    opaques_then_byte v{std::vector<opaque>(3), 0};

    CHECK(fmt::read(v, sb(bytes(7))) == 1 and v.after == 7 and v.v[0].x == 0);
}

// step_up in a nested object doesn't skip the rest of the enclosing one
static void step_up_nested() {
    record_then_byte v{};

    CHECK(fmt::read(v, sb(bytes(1, 0, 9))) == 3);
    CHECK(v.r.a == 1 and v.r.b == 0 and v.after == 9);
}

// step_up inside a value read with ctx.read() stays inside it
static void step_up_through_ctx_read() {
    wrapper_then_byte v{};

    CHECK(fmt::read(v, sb(bytes(1, 0, 9))) == 3);
    CHECK(v.w.r.a == 1 and v.w.r.b == 0 and v.after == 9);
}

// stop ends formatting altogether, even from deep inside
static void stop_ends_everything() {
    halting_then_byte v{};

    CHECK(fmt::read(v, sb(bytes(1, 2, 3))) == 1);
    CHECK(v.in.a == 1 and v.in.b == 0 and v.after == 0);
}

// stop inside a range element also stops the range and what follows it
static void stop_through_range() {
    halting_elems_then_byte v{std::vector<halting_elem>(2), 0};

    CHECK(fmt::read(v, sb(bytes(1, 2, 3))) == 1);
    CHECK(v.v[0].a == 1 and v.v[1].a == 0 and v.after == 0);
}

// formatting follows the same actions as parsing
static void format_follows_actions() {
    std::array<std::uint8_t, 4> out{};
    CHECK(fmt::write(record{1, {0}, 2}, sb(out)) == 2 and out == bytes(1, 0, 0, 0));
}

int main() {
    next_in_range();
    step_up_nested();
    step_up_through_ctx_read();
    stop_ends_everything();
    stop_through_range();
    format_follows_actions();
    return failures != 0;
}
