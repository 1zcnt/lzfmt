#include "check.hpp"

#include <iterator>
#include <list>
#include <sstream>
#include <stdexcept>

struct point {
    std::uint16_t x;
    std::uint16_t y;
};

// buffers of std::byte work like buffers of bytes
static void byte_buffer() {
    std::vector<std::byte> buf{std::byte{1}, std::byte{0}};
    std::uint16_t v;
    CHECK(le::parse(v, buf) == 2 and v == 1);
}

// buffers of wider elements: the count returned is in elements, not bytes
static void wide_elements() {
    std::array<std::uint16_t, 2> buf{0x0201, 0x0403};
    std::uint32_t v;
    CHECK(le::parse(v, buf) == 2 and v == 0x04030201);

    // a value must be a whole number of elements
    static_assert(!can_parse<le, std::uint8_t, decltype(buf)>);
}

// any input range can be parsed, not just contiguous ones
static void non_contiguous_input() {
    std::list<std::uint8_t> buf{1, 0, 2, 0};
    point p;
    CHECK(le::parse(p, buf) == 4 and p.x == 1 and p.y == 2);
}

// output-only ranges take std::byte
static void output_iterator() {
    std::vector<std::byte> out;
    std::ranges::subrange sink{std::back_inserter(out), std::unreachable_sentinel};

    CHECK(le::format(point{1, 2}, sink) == 4);
    CHECK(out == std::vector{std::byte{1}, std::byte{0}, std::byte{2}, std::byte{0}});
}

// a one-pass stream isn't stepped past the last element read, so reading a
// value never waits for input after it: the stream still yields that element
static void one_pass_stream() {
    std::istringstream ss("abc");
    std::ranges::subrange in{std::istreambuf_iterator<char>(ss), std::istreambuf_iterator<char>()};

    std::array<char, 2> v;
    CHECK(le::parse(v, in) == 2 and v == std::array{'a', 'b'});
    CHECK(ss.get() == 'b');
}

// running out of input or output throws, sized or not
static void eof() {
    std::uint32_t v;
    CHECK_THROWS(std::length_error, le::parse(v, bytes(1, 2, 3)));
    CHECK_THROWS(std::length_error, le::parse(v, std::list<std::uint8_t>{1, 2, 3}));

    std::istringstream ss("ab");
    std::ranges::subrange in{std::istreambuf_iterator<char>(ss), std::istreambuf_iterator<char>()};
    CHECK_THROWS(std::length_error, le::parse(v, in));

    std::array<std::uint8_t, 3> out{};
    CHECK_THROWS(std::length_error, le::format(v, out));
}

// the count returned is only what was read, not the whole buffer
static void partial_read() {
    std::uint16_t v;
    CHECK(le::parse(v, bytes(1, 2, 3, 4)) == 2 and v == 0x0201);
}

int main() {
    byte_buffer();
    wide_elements();
    non_contiguous_input();
    output_iterator();
    one_pass_stream();
    eof();
    partial_read();
    return failures != 0;
}
