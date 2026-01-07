#ifndef BEAST_RANDOM_RNGFILL_H_INCLUDED
#define BEAST_RANDOM_RNGFILL_H_INCLUDED

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
    constexpr std::size_t result_size = sizeof(result_type);

    std::uint8_t* const buffer_start = static_cast<std::uint8_t*>(buffer);
    std::size_t const complete_iterations = bytes / result_size;
    std::size_t const bytes_remaining = bytes % result_size;

    for (std::size_t count = 0; count < complete_iterations; ++count)
    {
        result_type const v = g();
        std::size_t const offset = count * result_size;
        std::memcpy(buffer_start + offset, &v, result_size);
    }

    if (bytes_remaining > 0)
    {
        result_type const v = g();
        std::size_t const offset = complete_iterations * result_size;
        std::memcpy(buffer_start + offset, &v, bytes_remaining);
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

#endif
