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

See `examples/` 

## Input

The buffer is a `std::streambuf`, or any type with its `char_type`, `sgetn()`
and `sputn()`. `stream_buffer()` from `util.hpp` gives the one of a stream, or
one over a range.

```cpp
std::ifstream file("header.bin", std::ios::binary);
fmt::read(h, *file.rdbuf());
```
