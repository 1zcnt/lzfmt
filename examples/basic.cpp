// a plain struct, written to an array and read back: no formatter needed

#include <lzfmt/binfmt.hpp>
#include <lzfmt/util.hpp>

#include <print>

using fmt = lz::fmt::binfmt<lz::fmt::params{.endianness = std::endian::big}>;

struct header {
    std::uint16_t               kind;
    std::array<std::uint8_t, 2> flags;
    struct { std::uint32_t a, b; } body;
};

int main() {
    std::array<std::uint8_t, 12> buf{};

    // write() and read() take a stream buffer: stream_buffer() makes one over a range
    auto written = fmt::write(header{1, {2, 3}, {4, 5}}, lz::fmt::stream_buffer(buf));
    std::println("{} bytes: {::02x}", written, buf);

    header h;
    auto read = fmt::read(h, lz::fmt::stream_buffer(buf));
    std::println("{} bytes: kind={} flags={} body={{{}, {}}}", read, h.kind, h.flags, h.body.a, h.body.b);
}
