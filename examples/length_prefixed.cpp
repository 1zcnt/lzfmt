// a hook: members annotated with length_prefixed<N> are stored with their size, an N, before them

#include <lzfmt/binfmt.hpp>
#include <lzfmt/util.hpp>

#include <print>
#include <sstream>
#include <string>
#include <vector>

template <std::integral N>
struct length_prefixed_t { };

template <std::integral N>
inline constexpr length_prefixed_t<N> length_prefixed;

// the N a member is annotated with, if it is: values that aren't members have no such subject
consteval std::meta::info prefix_of(std::meta::info subject) {
    //if (!is_nonstatic_data_member(subject))
    //    return {};
    auto a = lz::fmt::annotations_of_with_template(subject, ^^length_prefixed_t);
    return a.empty() ? std::meta::info{} : template_arguments_of(type_of(a[0]))[0];
}

// taken by the root over the formatter's read and write, for the contexts of such members
template <typename Ctx>
    requires (prefix_of(Ctx::subject) != std::meta::info{})
struct lz::fmt::hook<Ctx> {
    using N = [: prefix_of(Ctx::subject) :];

    static constexpr void read(auto &v, Ctx ctx) {
        N n;
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

using fmt = lz::fmt::binfmt<lz::fmt::params{.endianness = std::endian::little}>;

struct user {
    std::uint32_t id;
    [[=length_prefixed<std::uint8_t>]]  std::string                name;
    [[=length_prefixed<std::uint16_t>]] std::vector<std::uint16_t> scores;
};

int main() {
    std::stringbuf buf;

    fmt::write(user{7, "ada", {90, 75}}, buf);
    std::print("{} bytes:", buf.view().size());
    for (unsigned char c : buf.view())
        std::print(" {:02x}", c);
    std::println();

    user u;
    fmt::read(u, buf);
    std::println("id={} name={} scores={}", u.id, u.name, u.scores);
}
