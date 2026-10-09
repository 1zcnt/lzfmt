#include "check.hpp"

struct packet {
    std::uint8_t                kind;
    std::uint16_t               len;
    std::array<std::uint8_t, 2> data;
};

// parsing and formatting work in constant evaluation
static_assert([] {
    std::array<std::uint8_t, 5> buf{};
    le::write(packet{1, 0x0302, {4, 5}}, sb(buf));
    if (buf != bytes(1, 2, 3, 4, 5))
        return false;

    packet p{};
    return le::read(p, sb(buf)) == 5 and p.kind == 1 and p.len == 0x0302 and p.data[1] == 5;
}());

// running out of input isn't a constant expression
template <std::size_t N>
constexpr bool parses() {
    std::array<std::uint8_t, N> buf{};
    packet p{};
    return le::read(p, sb(buf)) == 5;
}

template <std::size_t N>
concept constant_parse = requires { typename std::integral_constant<bool, parses<N>()>; };

static_assert(constant_parse<5>);
static_assert(!constant_parse<4>);

int main() {
    return 0;
}
