#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace beast {

template <class Generator>
void
rngfill(void* const buffer, std::size_t const bytes, Generator& g)
{
    using result_type = typename Generator::result_type;
    constexpr std::size_t kRESULT_SIZE = sizeof(result_type);

    std::uint8_t* const bufferStart = static_cast<std::uint8_t*>(buffer);
    std::size_t const completeIterations = bytes / kRESULT_SIZE;
    std::size_t const bytesRemaining = bytes % kRESULT_SIZE;

    for (std::size_t count = 0; count < completeIterations; ++count)
    {
        result_type const v = g();
        std::size_t const offset = count * kRESULT_SIZE;
        std::memcpy(bufferStart + offset, &v, kRESULT_SIZE);
    }

    if (bytesRemaining > 0)
    {
        result_type const v = g();
        std::size_t const offset = completeIterations * kRESULT_SIZE;
        std::memcpy(bufferStart + offset, &v, bytesRemaining);
    }
}

template <
    class Generator,
    std::size_t N,
    class = std::enable_if_t<N % sizeof(typename Generator::result_type) == 0>>
void
rngfill(std::array<std::uint8_t, N>& a, Generator& g)
{
    using result_type = typename Generator::result_type;
    auto i = N / sizeof(result_type);
    result_type* p = reinterpret_cast<result_type*>(a.data());
    while (i--)
        *p++ = g();
}

}  // namespace beast
