#pragma once

#include "lzfmt.hpp"

#include <algorithm>
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

// like std::meta::annotations_of_with_type, for annotations of any
// specialization of the class template templ
consteval std::vector<std::meta::info> annotations_of_with_template(std::meta::info item, std::meta::info templ) {
    std::vector<std::meta::info> result;
    for (auto a : annotations_of(item)) {
        auto t = dealias(remove_cv(type_of(a)));
        if (has_template_arguments(t) and template_of(t) == templ)
            result.push_back(a);
    }
    return result;
}

// a range as a stream buffer: constexpr, and of any element type
template <std::input_or_output_iterator It, std::sentinel_for<It> S, typename E>
class range_buf {
    // stepping past an element of a one-pass stream may block waiting for the
    // next one: step only when needed, so a value can be the stream's last
    static constexpr bool lazy = std::input_iterator<It> and not std::forward_iterator<It>;
    static constexpr bool sized = std::forward_iterator<It> and std::sized_sentinel_for<S, It>;

    It   it_;
    S    end_;
    bool pending_ = false;   // lazy: it_ is at the element read last

    constexpr void settle() {
        if constexpr (lazy)
            if (std::exchange(pending_, false))
                ++it_;
    }

    constexpr auto fitting(std::streamsize n) const requires sized {
        return std::min<std::iter_difference_t<It>>(n, end_ - it_);
    }

public:
    using char_type = E;

    constexpr range_buf(It it, S end) : it_(std::move(it)), end_(std::move(end)) { }

    constexpr std::streamsize sgetn(E *out, std::streamsize n) requires std::input_iterator<It> {
        if constexpr (sized) {
            n = fitting(n);
            it_ = std::ranges::copy_n(std::move(it_), n, out).in;
            return n;
        } else {
            std::streamsize i = 0;
            for (; i < n; ++i) {
                settle();
                if (it_ == end_)
                    break;
                out[i] = *it_;
                if constexpr (lazy) pending_ = true;
                else                ++it_;
            }
            return i;
        }
    }

    constexpr std::streamsize sputn(const E *in, std::streamsize n) requires std::output_iterator<It, const E &> {
        if constexpr (sized) {
            n = fitting(n);
            it_ = std::ranges::copy_n(in, n, std::move(it_)).out;
            return n;
        } else {
            std::streamsize i = 0;
            for (; i < n and it_ != end_; ++i, ++it_)
                *it_ = in[i];
            return i;
        }
    }
};

namespace detail {
// output-only ranges have no value type
template <typename Rg>
struct buffer_element { using type = std::byte; };
template <typename Rg>
    requires requires { typename std::ranges::range_value_t<Rg>; }
struct buffer_element<Rg> { using type = std::ranges::range_value_t<Rg>; };

template <typename Rg>
using buffer_element_t = buffer_element<std::remove_reference_t<Rg>>::type;
} // namespace detail

// the buffer of anything stream-compatible: a stream buffer is its own
template <detail::streambuf_like Sb>
constexpr Sb &stream_buffer(Sb &sb) {
    return sb;
}

template <typename S>
    requires requires(S &s) { *s.rdbuf(); }
constexpr auto &stream_buffer(S &s) {
    return *s.rdbuf();
}

template <std::ranges::range Rg>
constexpr auto stream_buffer(Rg &r) {
    return range_buf<std::ranges::iterator_t<Rg &>, std::ranges::sentinel_t<Rg &>, detail::buffer_element_t<Rg>>{
        std::ranges::begin(r), std::ranges::end(r)};
}

} // namespace lz::fmt
