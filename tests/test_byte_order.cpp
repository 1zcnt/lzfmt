#include "check.hpp"

struct point {
    std::uint16_t x;
    std::uint32_t y;
};

// multi-byte numbers are read in the byte order of the params
static void parses_big_endian() {
    std::uint32_t v;
    CHECK(be::parse(v, bytes(1, 2, 3, 4)) == 4 and v == 0x01020304);

    std::int16_t i;
    CHECK(be::parse(i, bytes(0xff, 0xfe)) == 2 and i == -2);

    float f;
    auto  fb = std::bit_cast<std::array<std::uint8_t, 4>>(std::byteswap(std::bit_cast<std::uint32_t>(2.5f)));
    CHECK(be::parse(f, fb) == 4 and f == 2.5f);
}

// single bytes are the same in either byte order
static void single_bytes_unaffected() {
    std::uint8_t a, b;
    CHECK(be::parse(a, bytes(0x12)) == 1 and le::parse(b, bytes(0x12)) == 1 and a == b);
}

// members and range elements are each swapped, not the object as a whole
static void swaps_each_member() {
    point p;
    CHECK(be::parse(p, bytes(0, 1, 0, 0, 0, 2)) == 6 and p.x == 1 and p.y == 2);

    std::vector<std::uint16_t> v(3);
    CHECK(be::parse(v, bytes(0, 1, 0, 2, 1, 0)) == 6 and v == std::vector<std::uint16_t>{1, 2, 0x100});
}

// multi-byte numbers are written in the byte order of the params, without
// changing the value formatted
static void formats_big_endian() {
    std::array<std::uint8_t, 6> out{};

    std::uint32_t v = 0x01020304;
    CHECK(be::format(v, out) == 4 and std::ranges::equal(std::span(out).first(4), bytes(1, 2, 3, 4)));
    CHECK(v == 0x01020304);

    point p{1, 2};
    CHECK(be::format(p, out) == 6 and out == bytes(0, 1, 0, 0, 0, 2));
    CHECK(p.x == 1 and p.y == 2);
}

int main() {
    parses_big_endian();
    single_bytes_unaffected();
    swaps_each_member();
    formats_big_endian();
    return failures != 0;
}
