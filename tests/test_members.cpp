#include "check.hpp"

using std::meta::info;

struct pair2 {
    std::uint8_t a;
    std::uint8_t b;
};

template <auto P, std::same_as<pair2> T>
struct reversed_formatter : lz::fmt::base_formatter<P, T, reversed_formatter> {
    static consteval std::vector<info> members() { return {^^pair2::b, ^^pair2::a}; }

    template <info M> static constexpr auto &access_member(T &v)       { return v.[:M:]; }
    template <info M> static constexpr auto &access_member(const T &v) { return v.[:M:]; }
};

template <auto P, std::same_as<pair2> T>
struct only_b_formatter : reversed_formatter<P, T> {
    static consteval std::vector<info> members() { return {^^pair2::b}; }
};

struct words {
    std::array<std::uint16_t, 3> w;
};

// members are indices into an array, not data members
template <auto P, std::same_as<words> T>
struct indexed_formatter : lz::fmt::base_formatter<P, T, indexed_formatter> {
    static consteval std::vector<info> members() {
        return {std::meta::reflect_constant(2), std::meta::reflect_constant(0)};
    }

    template <info M> static constexpr auto &access_member(T &v)       { return v.w[[:M:]]; }
    template <info M> static constexpr auto &access_member(const T &v) { return v.w[[:M:]]; }
};

// bit-fields keep the builtin class formatter away
struct flags {
    std::uint8_t lo : 4;
    std::uint8_t hi : 4;
};

template <typename R>
struct flags_formatter {
    template <auto P, std::same_as<flags> T>
    struct type : lz::fmt::base_formatter<P, T, type> {
        static consteval std::vector<info> required() { return {^^R}; }
    };
};

// members are formatted in the order members() lists them
static void reordered() {
    pair2 p;
    CHECK(lz::fmt::binfmt<params{}, reversed_formatter>::parse(p, bytes(1, 2)) == 2);
    CHECK(p.a == 2 and p.b == 1);
}

// members left out of members() aren't formatted
static void subset() {
    pair2 p{};
    CHECK(lz::fmt::binfmt<params{}, only_b_formatter>::parse(p, bytes(1, 2)) == 1);
    CHECK(p.a == 0 and p.b == 1);
}

// members() may be any reflections: their type is what access_member returns
static void arbitrary_members() {
    using fmt = lz::fmt::binfmt<params{}, indexed_formatter>;

    words v{};
    CHECK(fmt::parse(v, bytes(1, 0, 2, 0)) == 4);
    CHECK(v.w == std::array<std::uint16_t, 3>{2, 0, 1});

    std::array<std::uint8_t, 4> out{};
    CHECK(fmt::format(words{{7, 8, 9}}, out) == 4 and out == bytes(9, 0, 7, 0));
}

// a formatter is only used if every type it requires can be formatted
static void requirements() {
    static_assert(!lz::fmt::binfmt<params{}>::check_type(^^flags));
    static_assert(lz::fmt::binfmt<params{}, flags_formatter<std::uint8_t>::type>::check_type(^^flags));
    static_assert(!lz::fmt::binfmt<params{}, flags_formatter<int *>::type>::check_type(^^flags));
}

int main() {
    reordered();
    subset();
    arbitrary_members();
    requirements();
    return failures != 0;
}
