// a hook: members annotated with enable_if{predicate} are stored only if the predicate
// holds for the header before them, which its formatter keeps in the run's storage

#include <lzfmt/binfmt.hpp>

#include <print>
#include <sstream>

struct header {
    std::uint8_t version;
    std::uint8_t flags;
};

struct enable_if {
    bool (*enabled)(const header &);
};

// storage lasts for one read() or write() and is reached through any context of it
template <auto P, std::same_as<header> T>
struct header_formatter : lz::fmt::class_formatter<P, T> {
    using storage_t = header;

    // read() comes after the members are read, write() before they are written
    static constexpr void read(T &h, auto ctx) {
        ctx.template get_storage<header_formatter>() = h;
    }

    static constexpr void write(const T &h, auto ctx) {
        ctx.template get_storage<header_formatter>() = h;
    }
};

template <typename Ctx>
concept conditional_member = is_nonstatic_data_member(Ctx::subject) and
                             !annotations_of_with_type(Ctx::subject, ^^enable_if).empty();

template <conditional_member Ctx>
struct lz::fmt::hook<Ctx> {
    using F      = [: Ctx::root::formatter_for(Ctx::type) :];
    using header = [: Ctx::root::formatter_for(^^::header) :];

    static constexpr enable_if condition =
        extract<enable_if>(annotations_of_with_type(Ctx::subject, ^^enable_if)[0]);

    static constexpr bool skip(auto ctx) {
        if (condition.enabled(ctx.template get_storage<header>()))
            return false;
        ctx.action = lz::fmt::visit::next;
        return true;
    }

    static constexpr void on_stream(auto ctx)            { if (!skip(ctx)) F::on_stream(ctx); }
    static constexpr void on_value(auto ctx)             { if (!skip(ctx)) F::on_value(ctx); }
    static constexpr void read(auto &v, auto ctx)        { if (!skip(ctx)) F::read(v, ctx); }
    static constexpr void write(const auto &v, auto ctx) { if (!skip(ctx)) F::write(v, ctx); }
};

using fmt = lz::fmt::binfmt<lz::fmt::params{.endianness = std::endian::big}, header_formatter>;

struct point {
    std::int16_t x, y;
};

struct record {
    header        h;
    std::uint16_t id;
    [[=enable_if{[](const header &h) { return h.version >= 2; }}]]      std::uint32_t created  = 0;
    [[=enable_if{[](const header &h) { return (h.flags & 1) != 0; }}]]  point         position = {-1, -1};
    std::uint8_t  checksum;
};

static void roundtrip(const record &r) {
    std::stringbuf buf;
    auto written = fmt::write(r, buf);
    std::print("version={} flags={}: {} bytes:", r.h.version, r.h.flags, written);
    for (unsigned char c : buf.view())
        std::print(" {:02x}", c);
    std::println();

    record back{};
    fmt::read(back, buf);
    std::println("  id={} created={} position=({}, {}) checksum={}",
                 back.id, back.created, back.position.x, back.position.y, back.checksum);
}

int main() {
    roundtrip({{1, 0}, 7, 1000, {3, 4}, 0xcc});
    roundtrip({{2, 0}, 7, 1000, {3, 4}, 0xcc});
    roundtrip({{2, 1}, 7, 1000, {3, 4}, 0xcc});
}
