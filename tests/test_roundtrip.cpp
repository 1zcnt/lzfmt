#include "check.hpp"

enum class kind : std::uint8_t { a = 1, b = 2 };

struct header {
    kind          k;
    std::uint16_t len;
    bool operator==(const header &) const = default;
};

struct sample {
    std::int8_t  i8;
    std::int64_t i64;
    float        f;
    double       d;
    bool operator==(const sample &) const = default;
};

struct message {
    header                       h;
    std::array<sample, 2>        samples;
    std::vector<std::uint32_t>   words;
    std::array<std::uint8_t, 2>  tail;
    bool operator==(const message &) const = default;
};

constexpr std::size_t message_size = 3 + 2 * 21 + 3 * 4 + 2;

static message make() {
    return {{kind::b, 0xbeef},
            {{{-1, -2, 1.25f, -3.5}, {127, 1ll << 40, -0.0f, 1e300}}},
            {1, 0x80000000u, 0xdeadbeef},
            {0xaa, 0x55}};
}

// formatting then parsing gives back the same value
template <typename F>
static void roundtrip() {
    auto m = make();
    std::vector<std::uint8_t> buf(message_size);
    CHECK(F::format(m, buf) == message_size);

    message back{};
    back.words.resize(3);
    CHECK(F::parse(back, buf) == message_size);
    CHECK(back == m);
}

// the byte orders differ only in multi-byte values
static void byte_orders_differ() {
    auto m = make();
    std::vector<std::uint8_t> l(message_size), b(message_size);
    le::format(m, l);
    be::format(m, b);
    CHECK(l[0] == b[0] and l[1] == b[2] and l[2] == b[1]);
    CHECK(l.back() == b.back());
}

// formatting doesn't change the value formatted
static void format_leaves_value() {
    auto m = make();
    std::vector<std::uint8_t> buf(message_size);
    be::format(m, buf);
    CHECK(m == make());
}

int main() {
    roundtrip<le>();
    roundtrip<be>();
    byte_orders_differ();
    format_leaves_value();
    return failures != 0;
}
