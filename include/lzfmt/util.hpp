#pragma once

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace lz::fmt {
class unix_file {
public:
    static constexpr std::size_t default_chunk_size = 4096;

    class exception : public std::runtime_error {
    public:
        enum class kind : int {
            noerror             = 0,

            bad_file            = static_cast<int>(std::errc::bad_file_descriptor),
            broken_pipe         = static_cast<int>(std::errc::broken_pipe),
            connection_refused  = static_cast<int>(std::errc::connection_refused),
            interrupted         = static_cast<int>(std::errc::interrupted),
            no_such_file        = static_cast<int>(std::errc::no_such_file_or_directory),
        no_such_device      = static_cast<int>(std::errc::no_such_device),
            permission_denied   = static_cast<int>(std::errc::permission_denied),
            would_block         = static_cast<int>(std::errc::resource_unavailable_try_again),

            invalid_handle      = std::numeric_limits<int>::min(),
            address_lookup,
            connect_failed,
            listen_failed,
            write_stalled,
        };

        kind error;

        exception(kind k, const std::string &what) : std::runtime_error(what), error(k) { }

        static exception from_errno(std::string_view where, int e = errno) {
            return {static_cast<kind>(e), std::string(where) + ": " + std::strerror(e)};
        }

        [[nodiscard]] bool is_errno() const noexcept { return std::to_underlying(error) > 0; }
        [[nodiscard]] bool is_local() const noexcept { return std::to_underlying(error) < 0; }

        [[nodiscard]] std::errc errc() const noexcept { return static_cast<std::errc>(std::to_underlying(error)); }
    };

    template <std::size_t ChunkSize>
    class chunk_view;

    template <std::size_t ChunkSize>
    class byte_sink;

    class mapping;

    unix_file() = default;

    explicit unix_file(int handle) : fd_(handle) {
        if (fd_ < 0)
            throw exception::from_errno("unix_file");
    }

    unix_file(const unix_file &) = delete;
    unix_file &operator=(const unix_file &) = delete;

    unix_file(unix_file &&other) noexcept : fd_(std::exchange(other.fd_, -1)) { }

    unix_file &operator=(unix_file &&other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ~unix_file() { reset(); }

    void reset() noexcept {
        if (fd_ != -1)
            ::close(std::exchange(fd_, -1));
    }

    [[nodiscard]] int  native_handle() const noexcept { return fd_; }
    [[nodiscard]] bool valid()         const noexcept { return fd_ != -1; }
    explicit operator bool()           const noexcept { return valid(); }

    [[nodiscard]] unix_file duplicate() const {
        return unix_file{check(::dup(checked_fd()), "dup")};
    }

    static unix_file open(const std::string &path, int flags = O_RDONLY, mode_t mode = 0666) {
        return unix_file{check(::open(path.c_str(), flags | O_CLOEXEC, mode), "open")};
    }

    static std::array<unix_file, 2> pipe() {
        int fds[2];
        check(::pipe2(fds, O_CLOEXEC), "pipe");
        return {unix_file{fds[0]}, unix_file{fds[1]}};
    }

    static unix_file socket(int domain, int type, int protocol = 0) {
        return unix_file{check(::socket(domain, type | SOCK_CLOEXEC, protocol), "socket")};
    }

    static unix_file connect_tcp(std::string_view host, std::string_view port) {
        auto addresses = lookup(std::string(host).c_str(), std::string(port), 0);
        for (addrinfo *p = addresses.get(); p; p = p->ai_next) {
            int s = ::socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol);
            if (s < 0)
                continue;
            if (::connect(s, p->ai_addr, p->ai_addrlen) == 0)
                return unix_file{s};
            ::close(s);
        }
        throw exception(exception::kind::connect_failed, "connect_tcp: unable to connect");
    }

    static unix_file listen_tcp(std::string_view port, int backlog = SOMAXCONN) {
        auto addresses = lookup(nullptr, std::string(port), AI_PASSIVE);
        for (addrinfo *p = addresses.get(); p; p = p->ai_next) {
            int s = ::socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol);
            if (s < 0)
                continue;
            int yes = 1;
            ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            if (::bind(s, p->ai_addr, p->ai_addrlen) == 0 and ::listen(s, backlog) == 0)
                return unix_file{s};
            ::close(s);
        }
        throw exception(exception::kind::listen_failed, "listen_tcp: unable to bind/listen");
    }

    [[nodiscard]] unix_file accept() const {
        return unix_file{retry([&] { return ::accept4(checked_fd(), nullptr, nullptr, SOCK_CLOEXEC); }, "accept")};
    }

    void shutdown(int how = SHUT_RDWR) const {
        check(::shutdown(checked_fd(), how), "shutdown");
    }



    [[nodiscard]] std::size_t read_some(std::span<std::byte> buf) const {
        return static_cast<std::size_t>(retry([&] { return ::read(checked_fd(), buf.data(), buf.size()); }, "read"));
    }

    [[nodiscard]] std::size_t write_some(std::span<const std::byte> buf) const {
        return static_cast<std::size_t>(retry([&] { return ::write(checked_fd(), buf.data(), buf.size()); }, "write"));
    }

    void write_all(std::span<const std::byte> buf) const {
        while (!buf.empty()) {
            auto n = write_some(buf);
            if (n == 0)
                throw exception(exception::kind::write_stalled, "write: no progress");
            buf = buf.subspan(n);
        }
    }

    template <std::size_t ChunkSize = default_chunk_size>
    [[nodiscard]] chunk_view<ChunkSize> chunks() const {
        return chunk_view<ChunkSize>{*this};
    }

    template <std::size_t ChunkSize = default_chunk_size>
    [[nodiscard]] auto out() const {
        return chunks<ChunkSize>() | std::views::join;
    }

    template <std::size_t ChunkSize = default_chunk_size>
    [[nodiscard]] byte_sink<ChunkSize> in() const {
        return byte_sink<ChunkSize>{*this};
    }

    [[nodiscard]] mapping map() const;

private:
    int fd_ = -1;

    int checked_fd() const {
        if (fd_ == -1)
            throw exception(exception::kind::invalid_handle, "unix_file: no file");
        return fd_;
    }

    static int check(int result, std::string_view what) {
        if (result < 0)
            throw exception::from_errno(what);
        return result;
    }

    template <typename Fn>
    static std::invoke_result_t<Fn> retry(Fn fn, std::string_view what) {
        for (;;) {
            auto result = fn();
            if (result >= 0)
                return result;
            if (errno != EINTR)
                throw exception::from_errno(what);
        }
    }

    using addrinfo_ptr = std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)>;

    static addrinfo_ptr lookup(const char *host, const std::string &port, int flags) {
        addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags    = flags;

        addrinfo *result = nullptr;
        if (int e = ::getaddrinfo(host, port.c_str(), &hints, &result); e != 0) {
            if (e == EAI_SYSTEM)
                throw exception::from_errno("getaddrinfo");
            throw exception(exception::kind::address_lookup, std::string("getaddrinfo: ") + ::gai_strerror(e));
        }
        return {result, &::freeaddrinfo};
    }
};

template <std::size_t ChunkSize>
class unix_file::chunk_view : public std::ranges::view_interface<chunk_view<ChunkSize>> {
    const unix_file                                  *file_ = nullptr;
    std::unique_ptr<std::array<std::byte, ChunkSize>> buf_;
    std::size_t                                       len_ = 0;

    void read_next() { len_ = file_->read_some(*buf_); }

public:
    class iterator {
        chunk_view *view_ = nullptr;

    public:
        using value_type      = std::span<std::byte>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;
        explicit iterator(chunk_view *view) : view_(view) { }

        std::span<std::byte> operator*() const { return {view_->buf_->data(), view_->len_}; }

        iterator &operator++() {
            view_->read_next();
            return *this;
        }
        void operator++(int) { ++*this; }

        friend bool operator==(const iterator &it, std::default_sentinel_t) { return it.view_->len_ == 0; }
    };

    chunk_view() = default;
    explicit chunk_view(const unix_file &file)
        : file_(&file), buf_(std::make_unique<std::array<std::byte, ChunkSize>>()) { }

    iterator begin() {
        read_next();
        return iterator{this};
    }
    std::default_sentinel_t end() const noexcept { return {}; }
};

template <std::size_t ChunkSize>
class unix_file::byte_sink {
    const unix_file                                  *file_ = nullptr;
    std::unique_ptr<std::array<std::byte, ChunkSize>> buf_;
    std::size_t                                       len_ = 0;

public:
    class iterator {
        byte_sink *sink_ = nullptr;

    public:
        using difference_type = std::ptrdiff_t;

        iterator() = default;
        explicit iterator(byte_sink *sink) : sink_(sink) { }

        iterator &operator*() { return *this; }
        iterator &operator=(std::byte b) {
            sink_->put(b);
            return *this;
        }
        iterator &operator++() { return *this; }
        iterator &operator++(int) { return *this; }
    };

    explicit byte_sink(const unix_file &file)
        : file_(&file), buf_(std::make_unique<std::array<std::byte, ChunkSize>>()) { }

    byte_sink(byte_sink &&) = default;
    byte_sink &operator=(byte_sink &&) = default;

    ~byte_sink() {
        try {
            flush();
        } catch (...) {
        }
    }

    iterator begin() { return iterator{this}; }
    std::unreachable_sentinel_t end() const noexcept { return {}; }

    void put(std::byte b) {
        (*buf_)[len_++] = b;
        if (len_ == ChunkSize)
            flush();
    }

    void flush() {
        if (buf_ and len_ > 0) {
            file_->write_all({buf_->data(), len_});
            len_ = 0;
        }
    }
};

} // namespace lz::fmt
