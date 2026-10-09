// anything with a streambuf's char_type, sgetn() and sputn() is a buffer

#include <lzfmt/binfmt.hpp>

#include <print>

using fmt = lz::fmt::binfmt<lz::fmt::params{.endianness = std::endian::big}>;

// takes everything and keeps nothing but the count
struct counter {
    using char_type = std::byte;

    std::streamsize count = 0;

    constexpr std::streamsize sputn(const std::byte *, std::streamsize n) {
        count += n;
        return n;
    }
};

template <typename T>
constexpr std::size_t size_of(const T &v) {
    return fmt::write(v, counter{});
}

// prints what it's given
struct hex_dump {
    using char_type = std::uint8_t;

    std::streamsize sputn(const std::uint8_t *p, std::streamsize n) {
        for (std::streamsize i = 0; i < n; ++i)
            std::print("{:02x} ", p[i]);
        return n;
    }
};

// yields 0, 1, 2, ... forever
struct iota {
    using char_type = std::uint8_t;

    std::uint8_t next = 0;

    constexpr std::streamsize sgetn(std::uint8_t *p, std::streamsize n) {
        for (std::streamsize i = 0; i < n; ++i)
            p[i] = next++;
        return n;
    }
};

struct packet {
    std::uint8_t                 kind;
    std::uint32_t                id;
    std::array<std::uint16_t, 2> ports;
};

// no padding is written, and it's all a constant expression
static_assert(size_of(packet{}) == 9);

int main() {
    packet p;
    fmt::read(p, iota{});
    std::println("kind={} id={:#x} ports={::#x}", p.kind, p.id, p.ports);

    fmt::write(p, hex_dump{});
    std::println("({} bytes)", size_of(p));
}
