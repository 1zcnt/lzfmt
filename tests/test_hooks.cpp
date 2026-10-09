#include "check.hpp"

struct pair {
    std::uint8_t a;
    std::uint8_t b;
};

struct holder {
    pair         p;
    std::uint8_t tail;
};

template <typename Ctx, auto M, typename T>
concept member_ctx = Ctx::subject == M and Ctx::type == ^^T;

// read and write of one member only
template <member_ctx<^^pair::b, std::uint8_t> Ctx>
struct lz::fmt::hook<Ctx> {
    static constexpr void read(std::uint8_t &v, Ctx ctx) { v = ctx.r.get() * 2; }
    static constexpr void write(const std::uint8_t &v, Ctx ctx) { ctx.r.put(v / 2); }
};

// on_value of a member changes a copy of a const value
template <member_ctx<^^holder::tail, std::uint8_t> Ctx>
struct lz::fmt::hook<Ctx> {
    static constexpr void on_value(Ctx ctx) { ctx.value ^= 0xff; }
};

// on_stream of a class member, and of a value that is no member
template <member_ctx<^^holder::p, pair> Ctx>
struct lz::fmt::hook<Ctx> {
    static constexpr void on_stream(Ctx ctx) { ctx.action = lz::fmt::visit::next; }
};

template <member_ctx<std::meta::info{}, std::uint16_t> Ctx>
struct lz::fmt::hook<Ctx> {
    static constexpr void read(std::uint16_t &v, Ctx) { v = 42; }
};

static void member_read_write() {
    pair p{};
    CHECK(le::read(p, sb(bytes(1, 2))) == 2 and p.a == 1 and p.b == 4);

    std::array<std::uint8_t, 2> out{};
    CHECK(le::write(p, sb(out)) == 2 and out == bytes(1, 2));
}

static void on_value_copies_const() {
    std::array<std::uint8_t, 3> out{};
    const holder h{{1, 4}, 0x0f};
    CHECK(le::write(h, sb(out)) == 3 and out == bytes(1, 2, 0xf0) and h.tail == 0x0f);
}

static void on_stream_skips_member() {
    holder h{};
    CHECK(le::read(h, sb(bytes(7, 8))) == 1 and h.p.a == 0 and h.tail == (7 ^ 0xff));
}

static void non_member_value() {
    std::uint16_t v = 0;
    CHECK(le::read(v, sb(bytes(1, 2))) == 0 and v == 42);

    // a member of that type keeps its formatter
    struct word { std::uint16_t w; } w{};
    CHECK(le::read(w, sb(bytes(1, 2))) == 2 and w.w == 0x0201);
}

int main() {
    member_read_write();
    on_value_copies_const();
    on_stream_skips_member();
    non_member_value();
    return failures != 0;
}
