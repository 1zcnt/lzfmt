# lzfmt

A header-only C++26 serialization library built on static reflection.

Types are read and written by *formatters*: small templates chosen at compile
time for each type. Built-in ones handle numbers, ranges and plain structs
(member by member, found through reflection), so most types need no code at
all. For anything else you write a formatter once, with the same code for both
directions, and it's used wherever that type appears.

Needs GCC 16 (`-freflection`) and CMake 4.3; `cmake -B build && cmake --build build`
builds the tests.

## Example

```cpp
#include <lzfmt/binfmt.hpp>

using fmt = lz::fmt::binfmt<lz::fmt::params{.endianness = std::endian::little}>;

struct header {
    std::uint16_t                kind;
    std::array<std::uint8_t, 2>  flags;
    struct { std::uint32_t a, b; } body;
};

std::array<std::uint8_t, 12> buf{};
std::size_t n = fmt::format(header{1, {2, 3}, {4, 5}}, buf);   // bytes written: 12

header h;
std::size_t m = fmt::parse(h, buf);                             // bytes read: 12
```

## A custom formatter

A length-prefixed `std::vector`: `fmt()` runs both when parsing and when
formatting, and `direction_split` covers the one step that differs.

```cpp
template <typename T> constexpr bool is_vector = false;
template <typename E> constexpr bool is_vector<std::vector<E>> = true;

template <auto P, typename T> requires is_vector<T>
struct sized_vector_formatter : lz::fmt::base_formatter<P, T, sized_vector_formatter> {
    template <typename Ctx>
    static constexpr void fmt(Ctx ctx) {
        auto n = static_cast<std::uint16_t>(ctx.value.size());
        ctx.fmt(n);
        lz::fmt::direction_split(ctx, [n](auto c) { c.value.resize(n); });
        for (auto &e : ctx.value)
            ctx.fmt(e);
    }
};

using sized = lz::fmt::binfmt<lz::fmt::params{}, sized_vector_formatter>;

struct message {
    std::uint8_t              kind;
    std::vector<std::uint8_t> body;
};

message m;
sized::parse(m, std::array<std::uint8_t, 6>{1, 3, 0, 4, 5, 6});
// m.kind == 1, m.body == {4, 5, 6}
```

Formatters can also skip or stop traversal, choose which members to visit,
and share per-run state; see `tests/` for examples.
