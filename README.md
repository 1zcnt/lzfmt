# lzfmt

[![tests](https://github.com/1zcnt/lzfmt/actions/workflows/tests.yml/badge.svg)](https://github.com/1zcnt/lzfmt/actions/workflows/tests.yml)
![Static Badge](https://img.shields.io/badge/Linux%20Droidvalds-approved-green?logo=redhat)
![Static Badge](https://img.shields.io/badge/Virus%20GCC-installed-green?logo=gnu)

A header-only C++26 serialization library built on static reflection.

Types are read and written by *formatters*: small templates chosen at compile
time for each type. Built-in ones handle numbers, ranges and plain structs
(member by member, found through reflection), so most types need no code at
all. For anything else you write a formatter once, and it's used wherever that
type appears.

Needs GCC 16 (`-freflection`) and CMake 4.3; `cmake -B build && cmake --build build`
builds the tests.

## Examples

See [examples](examples)

[zlib example](examples/zlib.cpp):
```cpp
struct chunk {
    std::uint32_t id;
    [[=zlib_compressed{}]]           std::array<std::uint8_t, 4096>  blocks;
    [[=zlib_compressed{.level = 9}]] std::array<std::uint16_t, 1024> heights;
    std::uint8_t  flags;
};


chunk c{.id = 7, .blocks = {}, .heights = {}, .flags = 1};
// fill it...
fmt::write(c, buf)
```

[hooks example(length-prefixed container)](examples/length_prefixed.cpp):
```cpp
// taken by the root over the formatter's read and write, for the contexts of such members
template <typename Ctx>
requires (prefix_of(Ctx::subject) != std::meta::info{})
struct lz::fmt::hook<Ctx> {
using N = [: prefix_of(Ctx::subject) :];

    static constexpr void read(auto &v, Ctx ctx) {
        N n{};
        ctx.read(n);
        v.resize(n);
        for (auto &e : v)
            ctx.read(e);
    }

    static constexpr void write(const auto &v, Ctx ctx) {
        ctx.write(static_cast<N>(v.size()));
        for (auto &e : v)
            ctx.write(e);
    }
};


struct user {
    std::uint32_t id;
    [[=length_prefixed<std::uint8_t>]]  std::string                name;
    [[=length_prefixed<std::uint16_t>]] std::vector<std::uint16_t> scores;
};


fmt::write(user{7, "ada", {90, 75}}, buf);
```


## Input

The buffer is a `std::streambuf`, or any type with its `char_type`, `sgetn()`
and `sputn()`. `stream_buffer()` from `util.hpp` gives the one of a stream, or
one over a range.

```cpp
std::ifstream file("header.bin", std::ios::binary);
fmt::read(h, *file.rdbuf());
```
