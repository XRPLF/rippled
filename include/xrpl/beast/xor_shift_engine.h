#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace beast {

namespace detail {

template <class = void>
class XorShiftEngine
{
public:
    using result_type = std::uint64_t;

    XorShiftEngine(XorShiftEngine const&) = default;
    XorShiftEngine&
    operator=(XorShiftEngine const&) = default;

    explicit XorShiftEngine(result_type val = 1977u);

    void
    seed(result_type seed);

    result_type
    operator()();

    static constexpr result_type
    min()
    {
        return std::numeric_limits<result_type>::min();
    }

    static constexpr result_type
    max()
    {
        return std::numeric_limits<result_type>::max();
    }

private:
    result_type s_[2]{};

    static result_type
    murmurhash3(result_type x);
};

template <class Unused>
XorShiftEngine<Unused>::XorShiftEngine(result_type val)
{
    seed(val);
}

template <class Unused>
void
XorShiftEngine<Unused>::seed(result_type seed)
{
    if (seed == 0)
        throw std::domain_error("invalid seed");
    s_[0] = murmurhash3(seed);
    s_[1] = murmurhash3(s_[0]);
}

template <class Unused>
auto
XorShiftEngine<Unused>::operator()() -> result_type
{
    result_type s1 = s_[0];
    result_type const s0 = s_[1];
    s_[0] = s0;
    s1 ^= s1 << 23;
    return (s_[1] = (s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26))) + s0;
}

template <class Unused>
auto
XorShiftEngine<Unused>::murmurhash3(result_type x) -> result_type
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    return x ^= x >> 33;
}

}  // namespace detail

/** XOR-shift Generator.

    Meets the requirements of UniformRandomNumberGenerator.

    Simple and fast RNG based on:
    http://xorshift.di.unimi.it/xorshift128plus.c
    does not accept seed==0
*/
using xor_shift_engine = detail::XorShiftEngine<>;

}  // namespace beast
