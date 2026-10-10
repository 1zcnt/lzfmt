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
    CHECK(le::read(v, sb(buf)) == 2 and v == 1);
}

// buffers of wider elements: the count returned is in elements, not bytes
static void wide_elements() {
    std::array<std::uint16_t, 2> buf{0x0201, 0x0403};
    std::uint32_t v;
    CHECK(le::read(v, sb(buf)) == 2 and v == 0x04030201);

    // a value must be a whole number of elements
    static_assert(!can_parse<le, std::uint8_t, decltype(buf)>);
}

// any input range can be parsed, not just contiguous ones
static void non_contiguous_input() {
    std::list<std::uint8_t> buf{1, 0, 2, 0};
    point p;
    CHECK(le::read(p, sb(buf)) == 4 and p.x == 1 and p.y == 2);
}

// output-only ranges take std::byte
static void output_iterator() {
    std::vector<std::byte> out;
    std::ranges::subrange sink{std::back_inserter(out), std::unreachable_sentinel};

    CHECK(le::write(point{1, 2}, sb(sink)) == 4);
    CHECK(out == std::vector{std::byte{1}, std::byte{0}, std::byte{2}, std::byte{0}});
}

// a one-pass stream isn't stepped past the last element read, so reading a
// value never waits for input after it: the stream still yields that element
static void one_pass_stream() {
    std::istringstream ss("abc");
    std::ranges::subrange in{std::istreambuf_iterator<char>(ss), std::istreambuf_iterator<char>()};

    std::array<char, 2> v;
    CHECK(le::read(v, sb(in)) == 2 and v == std::array{'a', 'b'});
    CHECK(ss.get() == 'b');
}

// a std::streambuf is used as it is, and left after what was read
static void streams() {
    std::stringstream ss("\x01\x02\x03");
    std::uint16_t v;
    CHECK(le::read(v, sb(ss)) == 2 and v == 0x0201);
    CHECK(ss.get() == 3);

    CHECK(le::write(point{'a', 'b'}, *ss.rdbuf()) == 4);
    CHECK(ss.str().substr(0, 4) == std::string_view("a\0b\0", 4));

    std::istringstream empty;
    CHECK_THROWS(std::length_error, le::read(v, sb(empty)));
}

// anything with a streambuf's char_type, sgetn() and sputn() is one
struct counting_buf {
    using char_type = std::uint8_t;

    std::uint8_t next = 0;

    constexpr std::streamsize sgetn(std::uint8_t *out, std::streamsize n) {
        for (std::streamsize i = 0; i < n; ++i)
            out[i] = next++;
        return n;
    }
};

template <typename S>
concept parses_from = requires(std::uint16_t &v, S &s) { le::read(v, s); };

static void streambuf_like() {
    counting_buf buf;
    std::uint16_t v;
    CHECK(le::read(v, buf) == 2 and v == 0x0100 and buf.next == 2);

    static_assert(can_parse<le, std::uint16_t, counting_buf>);
    static_assert(!can_format<le, std::uint16_t, counting_buf>);

    // a stream is taken for its buffer; a range isn't one
    static_assert(parses_from<counting_buf>);
    static_assert(parses_from<std::stringstream>);
    static_assert(!parses_from<std::vector<std::uint8_t>>);
}

// running out of input or output throws, sized or not
static void eof() {
    std::uint32_t v;
    CHECK_THROWS(std::length_error, le::read(v, sb(bytes(1, 2, 3))));
    CHECK_THROWS(std::length_error, le::read(v, sb(std::list<std::uint8_t>{1, 2, 3})));

    std::istringstream ss("ab");
    std::ranges::subrange in{std::istreambuf_iterator<char>(ss), std::istreambuf_iterator<char>()};
    CHECK_THROWS(std::length_error, le::read(v, sb(in)));

    std::array<std::uint8_t, 3> out{};
    CHECK_THROWS(std::length_error, le::write(v, sb(out)));
}

// the count returned is only what was read, not the whole buffer
static void partial_read() {
    std::uint16_t v;
    CHECK(le::read(v, sb(bytes(1, 2, 3, 4))) == 2 and v == 0x0201);
}

int main() {
    byte_buffer();
    wide_elements();
    non_contiguous_input();
    output_iterator();
    one_pass_stream();
    streams();
    streambuf_like();
    eof();
    partial_read();
    return failures != 0;
}
