// a hook: members annotated with zlib_compressed{} are stored deflated,
// as their size, the deflated size, and the deflated bytes

#include <lzfmt/binfmt.hpp>

#include <array>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>

#include <zlib.h>

struct zlib_compressed {
    int level = Z_DEFAULT_COMPRESSION;
};

template <typename Ctx>
concept zlib_member = is_nonstatic_data_member(Ctx::subject) and
                      !annotations_of_with_type(Ctx::subject, ^^zlib_compressed).empty();

// the member is formatted by the root as it would be without the hook, but into
// a buffer of its own: a value given to the root itself is no member, so has no hook
template <zlib_member Ctx>
struct lz::fmt::hook<Ctx> {
    static constexpr zlib_compressed params =
        extract<zlib_compressed>(annotations_of_with_type(Ctx::subject, ^^zlib_compressed)[0]);

    static void read(auto &v, Ctx ctx) {
        std::uint32_t size = 0, deflated_size = 0;
        ctx.read(size);
        ctx.read(deflated_size);

        std::string deflated(deflated_size, '\0');
        ctx.read(deflated);

        std::string raw(size, '\0');
        uLongf n = size;
        if (uncompress(reinterpret_cast<Bytef *>(raw.data()), &n,
                       reinterpret_cast<const Bytef *>(deflated.data()), deflated_size) != Z_OK or n != size)
            throw std::runtime_error("zlib: corrupt data");

        std::stringbuf buf(std::move(raw));
        Ctx::root::read(v, buf);
    }

    static void write(const auto &v, Ctx ctx) {
        std::stringbuf buf;
        Ctx::root::write(v, buf);
        auto raw = buf.view();

        std::string deflated(compressBound(raw.size()), '\0');
        uLongf n = deflated.size();
        if (compress2(reinterpret_cast<Bytef *>(deflated.data()), &n,
                      reinterpret_cast<const Bytef *>(raw.data()), raw.size(), params.level) != Z_OK)
            throw std::runtime_error("zlib: can't compress");
        deflated.resize(n);

        ctx.write(static_cast<std::uint32_t>(raw.size()));
        ctx.write(static_cast<std::uint32_t>(deflated.size()));
        ctx.write(deflated);
    }
};

using fmt = lz::fmt::binfmt<lz::fmt::params{.endianness = std::endian::little}>;

struct chunk {
    std::uint32_t id;
    [[=zlib_compressed{}]]           std::array<std::uint8_t, 4096>  blocks;
    [[=zlib_compressed{.level = 9}]] std::array<std::uint16_t, 1024> heights;
    std::uint8_t  flags;
};

int main() {
    chunk c{.id = 7, .blocks = {}, .heights = {}, .flags = 1};
    for (std::size_t i = 0; i < c.blocks.size(); ++i)
        c.blocks[i] = i / 512;
    for (std::size_t i = 0; i < c.heights.size(); ++i)
        c.heights[i] = 64 + i % 16;

    std::stringbuf buf;
    auto written = fmt::write(c, buf);
    std::println("{} bytes written, {} without the hook", written,
                 sizeof c.id + sizeof c.blocks + sizeof c.heights + sizeof c.flags);

    chunk back{};
    auto read = fmt::read(back, buf);
    std::println("{} bytes read, id={} flags={} same={}", read, back.id, back.flags,
                 back.blocks == c.blocks and back.heights == c.heights);
}
