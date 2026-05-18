#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <type_traits>

namespace xrpl {

// safe_cast adds compile-time checks to a static_cast to ensure that
// the destination can hold all values of the source.  This is particularly
// handy when the source or destination is an enumeration type.

template <class Src, class Dest>
concept SafeToCast = (std::is_integral_v<Src> && std::is_integral_v<Dest>) &&
    (std::is_signed_v<Src> || std::is_unsigned_v<Dest>) &&
    (std::is_signed_v<Src> != std::is_signed_v<Dest> ? sizeof(Dest) > sizeof(Src)
                                                     : sizeof(Dest) >= sizeof(Src));

template <class Dest, class Src>
constexpr std::enable_if_t<std::is_integral_v<Dest> && std::is_integral_v<Src>, Dest>
safeCast(Src s) noexcept
{
    static_assert(
        std::is_signed_v<Dest> || std::is_unsigned_v<Src>, "Cannot cast signed to unsigned");
    constexpr unsigned kNotSame = std::is_signed_v<Dest> != std::is_signed_v<Src>;
    static_assert(
        sizeof(Dest) >= sizeof(Src) + kNotSame,
        "Destination is too small to hold all values of source");
    return static_cast<Dest>(s);
}

template <class Dest, class Src>
constexpr std::enable_if_t<std::is_enum_v<Dest> && std::is_integral_v<Src>, Dest>
safeCast(Src s) noexcept
{
    return static_cast<Dest>(safeCast<std::underlying_type_t<Dest>>(s));
}

template <class Dest, class Src>
constexpr std::enable_if_t<std::is_integral_v<Dest> && std::is_enum_v<Src>, Dest>
safeCast(Src s) noexcept
{
    return safeCast<Dest>(static_cast<std::underlying_type_t<Src>>(s));
}

// unsafe_cast explicitly flags a static_cast as not necessarily able to hold
// all values of the source. It includes a compile-time check so that if
// underlying types become safe, it can be converted to a safe_cast.

template <class Dest, class Src>
constexpr std::enable_if_t<std::is_integral_v<Dest> && std::is_integral_v<Src>, Dest>
unsafeCast(Src s) noexcept
{
    static_assert(
        !SafeToCast<Src, Dest>,
        "Only unsafe if casting signed to unsigned or "
        "destination is too small");
    return static_cast<Dest>(s);
}

template <class Dest, class Src>
constexpr std::enable_if_t<std::is_enum_v<Dest> && std::is_integral_v<Src>, Dest>
unsafeCast(Src s) noexcept
{
    return static_cast<Dest>(unsafeCast<std::underlying_type_t<Dest>>(s));
}

template <class Dest, class Src>
constexpr std::enable_if_t<std::is_integral_v<Dest> && std::is_enum_v<Src>, Dest>
unsafeCast(Src s) noexcept
{
    return unsafeCast<Dest>(static_cast<std::underlying_type_t<Src>>(s));
}

template <class Dest, class Src>
    requires std::is_pointer_v<Dest>
inline Dest
safeDowncast(Src* s) noexcept
{
#ifdef NDEBUG
    return static_cast<Dest>(s);  // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
#else
    auto* result = dynamic_cast<Dest>(s);
    XRPL_ASSERT(result != nullptr, "xrpl::safeDowncast : pointer downcast is valid");
    return result;
#endif
}

template <class Dest, class Src>
    requires std::is_lvalue_reference_v<Dest>
inline Dest
safeDowncast(Src& s) noexcept
{
#ifndef NDEBUG
    XRPL_ASSERT(
        dynamic_cast<std::add_pointer_t<std::remove_reference_t<Dest>>>(&s) != nullptr,
        "xrpl::safeDowncast : reference downcast is valid");
#endif
    return static_cast<Dest>(s);  // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
}

}  // namespace xrpl
