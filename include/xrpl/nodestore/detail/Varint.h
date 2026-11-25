#pragma once

#include <nudb/detail/stream.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace xrpl::node_store {

// This is a variant of the base128 varint format from
// google protocol buffers:
// https://developers.google.com/protocol-buffers/docs/encoding#varints

// field tag
struct Varint;

// Metafunction to return largest
// possible size of T represented as varint.
// T must be unsigned
template <class T, bool = std::is_unsigned_v<T>>
struct VarintTraits;

template <class T>
struct VarintTraits<T, true>
{
    explicit VarintTraits() = default;

    static constexpr std::size_t kMax = ((8 * sizeof(T)) + 6) / 7;
};

// Returns: Number of bytes consumed or 0 on error,
//          if the buffer was too small or t overflowed.
//
template <class = void>
std::size_t
readVarint(void const* buf, std::size_t buflen, std::size_t& t)
{
    if (buflen == 0)
        return 0;
    t = 0;
    auto const* p = reinterpret_cast<std::uint8_t const*>(buf);
    std::size_t n = 0;
    while (p[n] & 0x80)
    {
        if (++n >= buflen)
            return 0;
    }
    if (++n > buflen)
        return 0;
    // Special case for 0
    if (n == 1 && *p == 0)
    {
        t = 0;
        return 1;
    }
    auto const used = n;
    while (n > 0)
    {
        --n;
        auto const d = p[n];
        auto const t0 = t;
        t *= 127;
        t += d & 0x7f;
        if (t <= t0)
            return 0;  // overflow
    }
    return used;
}

template <class T>
std::size_t
sizeVarint(T v)
    requires(std::is_unsigned_v<T>)
{
    std::size_t n = 0;
    do
    {
        v /= 127;
        ++n;
    } while (v != 0);
    return n;
}

template <class = void>
std::size_t
writeVarint(void* p0, std::size_t v)
{
    // NOLINTNEXTLINE(misc-const-correctness)
    auto* p = reinterpret_cast<std::uint8_t*>(p0);
    do
    {
        std::uint8_t d = v % 127;
        v /= 127;
        if (v != 0)
            d |= 0x80;
        *p++ = d;
    } while (v != 0);
    return p - reinterpret_cast<std::uint8_t*>(p0);
}

// input stream

template <class T>
void
read(nudb::detail::istream& is, std::size_t& u)
    requires(std::is_same_v<T, Varint>)
{
    auto p0 = is(1);
    auto p1 = p0;
    while (*p1++ & 0x80)
        is(1);
    readVarint(p0, p1 - p0, u);
}

// output stream

template <class T>
void
write(nudb::detail::ostream& os, std::size_t t)
    requires(std::is_same_v<T, Varint>)
{
    writeVarint(os.data(sizeVarint(t)), t);
}

}  // namespace xrpl::node_store
