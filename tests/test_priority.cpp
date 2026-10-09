#include "check.hpp"

// a byte, written twice; parsed from the first
template <auto P, std::same_as<std::uint8_t> T>
struct doubled_formatter : lz::fmt::base_formatter<P, T, doubled_formatter> {
    static constexpr void read(T &v, auto ctx)        { v = ctx.r.get(); ctx.r.get(); }
    static constexpr void write(const T &v, auto ctx) { ctx.r.put(v); ctx.r.put(v); }
};

// a byte, written with its complement
template <auto P, std::same_as<std::uint8_t> T>
struct checked_formatter : lz::fmt::base_formatter<P, T, checked_formatter> {
    static constexpr void read(T &v, auto ctx)        { v = ctx.r.get(); ctx.r.get(); }
    static constexpr void write(const T &v, auto ctx) { ctx.r.put(v); ctx.r.put(static_cast<std::uint8_t>(~v)); }
};

template <auto P, std::same_as<std::uint8_t> T>
struct [[=lz::fmt::priority::high]] urgent_checked_formatter : checked_formatter<P, T> { };

template <auto P, std::same_as<std::uint8_t> T>
struct [[=lz::fmt::priority::impl(1)]] late_doubled_formatter : doubled_formatter<P, T> { };

template <template <auto, typename> typename... Fmts>
static auto format_byte(std::uint8_t v) {
    std::array<std::uint8_t, 2> out{};
    lz::fmt::binfmt<params{}, Fmts...>::write(v, sb(out));
    return out;
}

// user formatters of default priority come before the builtin ones
static void user_before_builtin() {
    CHECK(format_byte<doubled_formatter>(5) == bytes(5, 5));
}

// among formatters of the same priority, the first listed is used
static void first_listed_wins() {
    CHECK(format_byte<doubled_formatter, checked_formatter>(5) == bytes(5, 5));
    CHECK(format_byte<checked_formatter, doubled_formatter>(5) == bytes(5, 0xfa));
}

// a lower priority wins, wherever it's listed
static void lower_priority_wins() {
    CHECK(format_byte<doubled_formatter, urgent_checked_formatter>(5) == bytes(5, 0xfa));
}

// priority is inherited from the formatter's bases
static void inherited_priority() {
    CHECK(format_byte<urgent_checked_formatter>(5) == bytes(5, 0xfa));
}

// a user formatter behind the builtin one is never used
static void behind_builtin() {
    CHECK(format_byte<late_doubled_formatter>(5) == bytes(5, 0));
}

int main() {
    user_before_builtin();
    first_listed_wins();
    lower_priority_wins();
    inherited_priority();
    behind_builtin();
    return failures != 0;
}
