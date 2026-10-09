// a user formatter: strings and vectors are stored with their size before them

#include <lzfmt/binfmt.hpp>

#include <print>
#include <sstream>
#include <string>
#include <vector>

template <typename T>
concept resizable = std::ranges::range<T> and requires(T &v, std::size_t n) { v.resize(n); };

// taken by the root for every type it accepts, anywhere such a value appears
template <auto P, resizable T>
struct sized_formatter : lz::fmt::base_formatter<P, T, sized_formatter> {
    static constexpr void read(T &v, auto ctx) {
        std::uint16_t n;
        ctx.read(n);
        v.resize(n);
        for (auto &e : v)
            ctx.read(e);
    }

    static constexpr void write(const T &v, auto ctx) {
        ctx.write(static_cast<std::uint16_t>(v.size()));
        for (auto &e : v)
            ctx.write(e);
    }
};

using fmt = lz::fmt::binfmt<lz::fmt::params{.endianness = std::endian::little}, sized_formatter>;

struct user {
    std::uint32_t            id;
    std::string              name;
    std::vector<std::string> tags;
};

int main() {
    std::stringbuf buf;

    fmt::write(user{7, "ada", {"admin", "dev"}}, buf);
    std::print("{} bytes:", buf.view().size());
    for (unsigned char c : buf.view())
        std::print(" {:02x}", c);
    std::println();

    user u;
    fmt::read(u, buf);
    std::println("id={} name={} tags={}", u.id, u.name, u.tags);
}
