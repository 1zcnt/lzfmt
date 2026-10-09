#include "check.hpp"

enum class color : std::uint16_t { red = 0x0102 };

// unsigned and signed integers of every size round-trip through their bytes
static void integers() {
    std::uint8_t  u8;
    std::uint16_t u16;
    std::uint32_t u32;
    std::uint64_t u64;
    std::int32_t  i32;

    CHECK(le::read(u8, sb(bytes(0xab))) == 1 and u8 == 0xab);
    CHECK(le::read(u16, sb(bytes(1, 2))) == 2 and u16 == 0x0201);
    CHECK(le::read(u32, sb(bytes(1, 2, 3, 4))) == 4 and u32 == 0x04030201);
    CHECK(le::read(u64, sb(bytes(1, 2, 3, 4, 5, 6, 7, 8))) == 8 and u64 == 0x0807060504030201);
    CHECK(le::read(i32, sb(bytes(0xff, 0xff, 0xff, 0xff))) == 4 and i32 == -1);

    std::array<std::uint8_t, 4> out{};
    CHECK(le::write(std::uint32_t{0x0a0b0c0d}, sb(out)) == 4 and out == bytes(0x0d, 0x0c, 0x0b, 0x0a));
    CHECK(le::write(std::int16_t{-2}, sb(out)) == 2 and out[0] == 0xfe and out[1] == 0xff);
}

// floating point values are stored as their bit pattern
static void floats() {
    std::array<std::uint8_t, 8> buf{};
    float  f = 0;
    double d = 0;

    CHECK(le::write(1.5f, sb(buf)) == 4);
    CHECK(le::read(f, sb(buf)) == 4 and f == 1.5f);

    CHECK(le::write(-0.25, sb(buf)) == 8);
    CHECK(le::read(d, sb(buf)) == 8 and d == -0.25);
}

// bool, char and std::byte are single bytes
static void single_bytes() {
    bool      b = false;
    char      c = 0;
    std::byte y{};

    CHECK(le::read(b, sb(bytes(1))) == 1 and b);
    CHECK(le::read(c, sb(bytes('x'))) == 1 and c == 'x');
    CHECK(le::read(y, sb(bytes(0x5a))) == 1 and y == std::byte{0x5a});
}

// enums are formatted as their underlying type only when enum_is_integral is set
static void enums() {
    color c{};
    CHECK(le::read(c, sb(bytes(2, 1))) == 2 and c == color::red);

    std::array<std::uint8_t, 2> out{};
    CHECK(le::write(color::red, sb(out)) == 2 and out == bytes(2, 1));

    static_assert(!lz::fmt::binfmt<params{.enum_is_integral = false}>::check_type(^^color));
}

// a const value can be formatted from but not parsed into
static void const_values() {
    using buf = std::array<std::uint8_t, 4>;

    static_assert(can_format<le, const std::uint32_t, buf>);
    static_assert(!can_parse<le, const std::uint32_t, buf>);
}

// types without a formatter are rejected
static void unsupported_types() {
    static_assert(!le::check_type(^^long double));
    static_assert(!le::check_type(^^int *));
    static_assert(!le::check_type(^^void));
}

int main() {
    integers();
    floats();
    single_bytes();
    enums();
    const_values();
    unsupported_types();
    return failures != 0;
}
