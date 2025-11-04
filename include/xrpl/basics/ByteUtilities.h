#ifndef XRPL_BASICS_BYTEUTILITIES_H_INCLUDED
#define XRPL_BASICS_BYTEUTILITIES_H_INCLUDED

namespace ripple {

template <class T>
constexpr auto
kilobytes(T value) noexcept
{
    return value * 1024;
}

template <class T>
constexpr auto
megabytes(T value) noexcept
{
    return kilobytes(kilobytes(value));
}

static_assert(kilobytes(2) == 2048, "kilobytes(2) == 2048");
static_assert(megabytes(3) == 3145728, "megabytes(3) == 3145728");
}  // namespace ripple

#endif
