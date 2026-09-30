#include "check.hpp"

#include <list>
#include <string>

struct point {
    std::uint8_t x;
    std::uint8_t y;
};

// a range is formatted as its elements, with no size; parsing fills as many
// elements as the range already holds
static void no_size_prefix() {
    std::vector<std::uint16_t> v{1, 2};
    std::array<std::uint8_t, 4> out{};
    CHECK(le::format(v, out) == 4 and out == bytes(1, 0, 2, 0));

    std::vector<std::uint16_t> back(2);
    CHECK(le::parse(back, out) == 4 and back == v);
}

// an empty range reads and writes nothing
static void empty_range() {
    std::vector<std::uint32_t> v;
    CHECK(le::parse(v, bytes(1, 2, 3, 4)) == 0);
}

// ranges of classes are formatted element by element
static void range_of_classes() {
    std::vector<point> v(2);
    CHECK(le::parse(v, bytes(1, 2, 3, 4)) == 4);
    CHECK(v[0].x == 1 and v[0].y == 2 and v[1].x == 3 and v[1].y == 4);
}

// nested ranges are flattened
static void nested_ranges() {
    std::vector<std::vector<std::uint8_t>> v{{1}, {2, 3}};
    std::array<std::uint8_t, 3> out{};
    CHECK(le::format(v, out) == 3 and out == bytes(1, 2, 3));
}

// std::string is a range of chars, not a class
static void strings() {
    std::string s(3, ' ');
    CHECK(le::parse(s, bytes('a', 'b', 'c')) == 3 and s == "abc");
}

// non-contiguous ranges work too
static void list() {
    std::list<std::uint16_t> l(2);
    CHECK(le::parse(l, bytes(1, 0, 2, 0)) == 4 and l.front() == 1 and l.back() == 2);
}

int main() {
    no_size_prefix();
    empty_range();
    range_of_classes();
    nested_ranges();
    strings();
    list();
    return failures != 0;
}
