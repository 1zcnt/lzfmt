// a std::streambuf is used as it is: here a file's

#include <lzfmt/binfmt.hpp>

#include <filesystem>
#include <fstream>
#include <print>

using fmt = lz::fmt::binfmt<lz::fmt::params{.endianness = std::endian::little}>;

struct sample {
    std::uint32_t        time;
    std::array<float, 3> position;
};

int main() {
    auto path = std::filesystem::temp_directory_path() / "lzfmt_example.bin";

    {
        std::filebuf file;
        file.open(path, std::ios::out | std::ios::binary);

        for (std::uint32_t t = 0; t < 3; ++t)
            fmt::write(sample{t, {1.5f * t, 2.5f * t, 0}}, file);
    }

    // a stream is read through its buffer
    std::ifstream file(path, std::ios::binary);
    try {
        for (;;) {
            sample s;
            fmt::read(s, file);
            std::println("t={} position={}", s.time, s.position);
        }
    } catch (const std::length_error &) {
        // the file ended
    }

    std::filesystem::remove(path);
}
