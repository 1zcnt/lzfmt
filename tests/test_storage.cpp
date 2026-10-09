#include "check.hpp"

// written as how many ticks were formatted before it, counting itself
struct tick { };

template <auto P, std::same_as<tick> T>
struct tick_formatter : lz::fmt::base_formatter<P, T, tick_formatter> {
    using storage_t = std::uint8_t;

    static constexpr void write(const T &, auto ctx) {
        auto n = ++ctx.template get_storage<tick_formatter>();
        ctx.r.put(n);
    }
};

template <int N>
struct tagged { };

template <typename T> constexpr bool is_tagged = false;
template <int N> constexpr bool is_tagged<tagged<N>> = true;

// like tick, one counter per N
template <auto P, typename T> requires is_tagged<T>
struct tagged_formatter : lz::fmt::base_formatter<P, T, tagged_formatter> {
    using storage_t = std::uint8_t;

    static constexpr void write(const T &, auto ctx) {
        ctx.r.put(++ctx.template get_storage<tagged_formatter>());
    }
};

// a size, kept for the payload after it
struct length {
    std::uint8_t n;
};

struct payload {
    std::vector<std::uint8_t> bytes;
};

template <auto P, std::same_as<length> T>
struct length_formatter : lz::fmt::base_formatter<P, T, length_formatter> {
    using storage_t = std::size_t;

    static constexpr void read(T &len, auto ctx) {
        ctx.read(len.n);
        ctx.template get_storage<length_formatter>() = len.n;
    }

    static constexpr void write(const T &len, auto ctx) {
        ctx.write(len.n);
    }
};

template <auto P, std::same_as<payload> T>
struct payload_formatter : lz::fmt::base_formatter<P, T, payload_formatter> {
    static constexpr void read(T &p, auto ctx) {
        p.bytes.resize(ctx.template get_storage<length_formatter<P, length>>());
        for (auto &b : p.bytes)
            ctx.read(b);
    }

    static constexpr void write(const T &p, auto ctx) {
        for (auto &b : p.bytes)
            ctx.write(b);
    }
};

struct packet {
    length  len;
    payload data;
};

struct three_ticks {
    tick a, b, c;
};

struct mixed_tags {
    tagged<0> a;
    tagged<1> b;
    tagged<0> c;
};

struct tick_tree {
    tick                   t;
    std::vector<tick_tree> kids;
};

struct plain {
    std::uint8_t  a;
    std::uint16_t b;
};

using fmt = lz::fmt::binfmt<params{}, tick_formatter, tagged_formatter, length_formatter, payload_formatter>;

template <typename T, std::size_t N = 3>
static auto format(const T &v) {
    std::array<std::uint8_t, N> out{};
    fmt::write(v, sb(out));
    return out;
}

// all values of a formatter specialization share its storage
static void shared_by_values() {
    CHECK(format(three_ticks{}) == bytes(1, 2, 3));
}

// every run starts with fresh storage
static void fresh_each_run() {
    CHECK(format(three_ticks{}) == bytes(1, 2, 3));
    CHECK(format(three_ticks{}) == bytes(1, 2, 3));
}

// each specialization of a formatter template has its own storage
static void per_specialization() {
    CHECK(format(mixed_tags{}) == bytes(1, 1, 2));
}

// storage reaches values formatted as range elements, through ctx.write()
static void through_ranges() {
    CHECK(format(std::vector<tick>(3)) == bytes(1, 2, 3));
}

// recursive types get one storage per specialization too
static void recursive() {
    tick_tree t{{}, {{{}, {}}, {{}, {{{}, {}}}}}};
    CHECK(format<tick_tree, 4>(t) == bytes(1, 2, 3, 4));
}

// one formatter can read what another stored
static void between_formatters() {
    packet p{};
    CHECK(fmt::read(p, sb(bytes(3, 7, 8, 9, 10))) == 4);
    CHECK(p.len.n == 3 and p.data.bytes == std::vector<std::uint8_t>{7, 8, 9});
}

// only formatters with a storage_t get storage, each once
static void only_formatters_with_storage() {
    static_assert(std::tuple_size_v<fmt::storage_for<plain>> == 0);
    static_assert(std::tuple_size_v<fmt::storage_for<three_ticks>> == 1);
    static_assert(std::tuple_size_v<fmt::storage_for<mixed_tags>> == 2);
    static_assert(std::tuple_size_v<fmt::storage_for<packet>> == 1);
}

// storage works in constant evaluation
static_assert([] {
    std::array<std::uint8_t, 3> out{};
    fmt::write(three_ticks{}, sb(out));
    return out == bytes(1, 2, 3);
}());

int main() {
    shared_by_values();
    fresh_each_run();
    per_specialization();
    through_ranges();
    recursive();
    between_formatters();
    only_formatters_with_storage();
    return failures != 0;
}
