#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace beast {

template <class Generator>
void
rngfill(void* const buffer, std::size_t const bytes, Generator& g)
{
    using result_type = Generator::result_type;
    constexpr std::size_t kResultSize = sizeof(result_type);

    auto* const bufferStart = static_cast<std::uint8_t*>(buffer);
    std::size_t const completeIterations = bytes / kResultSize;
    std::size_t const bytesRemaining = bytes % kResultSize;

    for (std::size_t count = 0; count < completeIterations; ++count)
    {
        result_type const v = g();
        std::size_t const offset = count * kResultSize;
        std::memcpy(bufferStart + offset, &v, kResultSize);
    }

    if (bytesRemaining > 0)
    {
        result_type const v = g();
        std::size_t const offset = completeIterations * kResultSize;
        std::memcpy(bufferStart + offset, &v, bytesRemaining);
    }
}

template <class Generator, std::size_t N>
void
rngfill(std::array<std::uint8_t, N>& a, Generator& g)
    requires(N % sizeof(typename Generator::result_type) == 0)
{
    using result_type = Generator::result_type;
    auto i = N / sizeof(result_type);
    auto* p = reinterpret_cast<result_type*>(a.data());
    while (i--)
        *p++ = g();
}

}  // namespace beast
