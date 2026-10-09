#include <lzfmt/lzfmt.hpp>

#include <print>

#include "lzfmt/binfmt.hpp"
#include "lzfmt/util.hpp"

struct aboba {
    uint16_t a;
    struct aboba2 {
        uint8_t b;
        uint8_t c;
    } bc;
};

struct cfg {
    std::endian endianness          = std::endian::native;
    bool        enum_is_integral    = false;
};

int main() {
    std::vector<uint8_t> x = {0, 0, 1, 0};

    using Fmt = lz::fmt::binfmt<cfg{}>;
    aboba a;

    Fmt::read(a, lz::fmt::stream_buffer(x));

    std::println("{} {} {}", a.a, a.bc.b, a.bc.c);

    return 0;
}