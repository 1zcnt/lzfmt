#include "check.hpp"

struct inner {
    std::uint8_t b;
    std::uint8_t c;
};

struct outer {
    std::uint16_t a;
    inner         bc;
    std::uint8_t  d;
};

struct with_arrays {
    std::uint8_t                c_array[3];
    std::array<std::uint16_t, 2> std_array;
};

struct grid {
    std::uint8_t cells[2][2];
};

struct with_static {
    static inline std::uint32_t ignored = 0;
    std::uint8_t                v;
};

class with_private {
    std::uint8_t x = 7;
public:
    std::uint8_t get() const { return x; }
};

struct empty { };

struct with_bit_field {
    std::uint8_t a : 4;
    std::uint8_t b : 4;
};

union either {
    std::uint8_t  a;
    std::uint16_t b;
};

struct node {
    std::vector<node> kids;
    std::uint8_t      v;
};

// members are formatted in declaration order, nested classes in place
static void nested_members() {
    outer o;
    CHECK(le::parse(o, bytes(1, 0, 2, 3, 4)) == 5);
    CHECK(o.a == 1 and o.bc.b == 2 and o.bc.c == 3 and o.d == 4);

    std::array<std::uint8_t, 5> out{};
    CHECK(le::format(outer{0x0201, {3, 4}, 5}, out) == 5 and out == bytes(1, 2, 3, 4, 5));
}

// C and std arrays members are formatted element by element
static void array_members() {
    with_arrays w;
    CHECK(le::parse(w, bytes(1, 2, 3, 4, 0, 5, 0)) == 7);
    CHECK(w.c_array[0] == 1 and w.c_array[2] == 3 and w.std_array[0] == 4 and w.std_array[1] == 5);

    std::array<std::uint8_t, 7> out{};
    CHECK(le::format(w, out) == 7 and out == bytes(1, 2, 3, 4, 0, 5, 0));
}

// multi-dimensional C arrays are flattened, row by row
static void multi_dimensional_arrays() {
    std::array<std::uint8_t, 4> out{};
    CHECK(le::format(grid{{{1, 2}, {3, 4}}}, out) == 4 and out == bytes(1, 2, 3, 4));

    grid g{};
    CHECK(le::parse(g, out) == 4 and g.cells[0][1] == 2 and g.cells[1][0] == 3);

    static_assert([] {
        std::array<std::uint8_t, 4> buf{};
        le::format(grid{{{5, 6}, {7, 8}}}, buf);
        return buf == bytes(5, 6, 7, 8);
    }());
}

// static data members aren't part of the object
static void static_members_ignored() {
    with_static w;
    CHECK(le::parse(w, bytes(9)) == 1 and w.v == 9 and with_static::ignored == 0);
}

// private members are formatted too: access checks are skipped
static void private_members() {
    std::array<std::uint8_t, 1> out{};
    CHECK(le::format(with_private{}, out) == 1 and out[0] == 7);

    with_private p;
    CHECK(le::parse(p, bytes(3)) == 1 and p.get() == 3);
}

// an empty class takes no bytes
static void empty_class() {
    empty e;
    std::array<std::uint8_t, 0> buf{};
    CHECK(le::parse(e, buf) == 0);
    CHECK(le::format(e, buf) == 0);
}

// classes with bit-fields and unions have no formatter
static void unsupported_classes() {
    static_assert(!le::check_type(^^with_bit_field));
    static_assert(!le::check_type(^^either));
}

// a class may contain itself through a range
static void recursive_class() {
    node n{{{{}, 1}, {{}, 2}}, 3};
    std::array<std::uint8_t, 3> out{};
    CHECK(le::format(n, out) == 3 and out == bytes(1, 2, 3));

    node back{std::vector<node>(2), 0};
    CHECK(le::parse(back, out) == 3);
    CHECK(back.kids[0].v == 1 and back.kids[1].v == 2 and back.v == 3);
}

int main() {
    nested_members();
    array_members();
    multi_dimensional_arrays();
    static_members_ignored();
    private_members();
    empty_class();
    unsupported_classes();
    recursive_class();
    return failures != 0;
}
