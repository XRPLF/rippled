#pragma once

#include <xrpl/beast/utility/instrumentation.h>  // IWYU pragma: keep

#include <concepts>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace xrpl {

/** Every value of @p Src can be represented by @p Dest.

    Given two integral types, the cast is safe when the destination can
    hold every possible value of the source.

    Comparing the bounds requires care: we use @c std::cmp_less_equal and
    @c std::cmp_greater_equal; the plain relational operators would apply
    arithmetic conversions, resulting in incorrect results when comparing
    across signedness.

    Because @c std::cmp_* requires standard signed or unsigned integer
    arguments, which excludes character types and bool, we first widen
    all type bounds to the maximum-width integer type while preserving
    signedness.

    @note Extended integer types, like __int128 on gcc, cannot be safely
          widened to a standard integer type, so the concept will reject
          them.
*/
template <typename Src, typename Dest>
concept SafeToCast = std::is_integral_v<Src> && std::is_integral_v<Dest> && []() consteval {
    using WideSrc = std::conditional_t<std::is_signed_v<Src>, std::intmax_t, std::uintmax_t>;
    using WideDest = std::conditional_t<std::is_signed_v<Dest>, std::intmax_t, std::uintmax_t>;

    // Note: this guard must be an evaluated branch, and not a
    // static_assert. The lambda body is outside the immediate
    // context, so a substitution-time failure here would be a
    // hard error rather than leaving the concept unsatisfied.
    if constexpr (sizeof(Src) > sizeof(WideSrc) || sizeof(Dest) > sizeof(WideDest))
        return false;
    else
        return std::cmp_less_equal(
                   static_cast<WideDest>(std::numeric_limits<Dest>::min()),
                   static_cast<WideSrc>(std::numeric_limits<Src>::min())) &&
            std::cmp_greater_equal(
                   static_cast<WideDest>(std::numeric_limits<Dest>::max()),
                   static_cast<WideSrc>(std::numeric_limits<Src>::max()));
}();

/** Compile-time-checked static_cast that rejects non-value preserving casts.

    @note There is deliberately no enum-to-enum overload, and no overload
          returning the underlying type of an enum. For the latter, use
          @c std::to_underlying.
*/
/** @{ */
template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_integral_v<Src>)
constexpr Dest
safeCast(Src s) noexcept
{
    static_assert(
        SafeToCast<Src, Dest>, "This cast is not value-preserving. Please use unsafeCast instead.");
    return static_cast<Dest>(s);
}

template <typename Dest, typename Src>
    requires(std::is_enum_v<Dest> && std::is_integral_v<Src>)
constexpr Dest
safeCast(Src s) noexcept
{
    return static_cast<Dest>(safeCast<std::underlying_type_t<Dest>>(s));
}

template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_enum_v<Src>)
constexpr Dest
safeCast(Src s) noexcept
{
    return safeCast<Dest>(std::to_underlying(s));
}
/** @} */

/** Integral-to-integral cast that is known to be narrowing or sign-erasing.

    This explicitly flags a conversion that can lose information for some
    values of the source type, where the call site accepts that loss (or
    truncation is the intended behavior).

    The compile-time check ensures the cast remains "unsafe": if the types
    involved later change such that the conversion becomes inherently
    value-preserving, the static assertion fires with instructions to
    migrate the call site to @ref safeCast.

    If the conversion's safety depends on a runtime precondition rather
    than on the types, or varies across instantiations of generic code,
    use @ref checkedCast instead.
*/
/** @{ */
template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_integral_v<Src>)
constexpr Dest
unsafeCast(Src s) noexcept
{
    static_assert(
        !SafeToCast<Src, Dest>, "This cast is value-preserving. Please use safeCast instead.");
    return static_cast<Dest>(s);
}

template <typename Dest, typename Src>
    requires(std::is_enum_v<Dest> && std::is_integral_v<Src>)
constexpr Dest
unsafeCast(Src s) noexcept
{
    return static_cast<Dest>(unsafeCast<std::underlying_type_t<Dest>>(s));
}

template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_enum_v<Src>)
constexpr Dest
unsafeCast(Src s) noexcept
{
    return unsafeCast<Dest>(std::to_underlying(s));
}
/** @} */

/** Integral-to-integral cast when the caller has performed a bounds check.

    This documents that a runtime precondition or external invariant, which
    is not necessarily visible to the compiler, guarantees that the requested
    conversion is value-preserving for the values that can actually occur.

    This is primarily meant for generic code, where the same expression may
    be value-preserving for one instantiation but not for another, making
    both @ref safeCast and @ref unsafeCast unusable.

    Unlike @ref safeCast and @ref unsafeCast, this imposes no static check
    on the type relationship: the caller's claim is about runtime values,
    not about types.
*/
/** @{ */
template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_integral_v<Src>)
constexpr Dest
checkedCast(Src s) noexcept
{
    return static_cast<Dest>(s);
}

template <typename Dest, typename Src>
    requires(std::is_enum_v<Dest> && std::is_integral_v<Src>)
constexpr Dest
checkedCast(Src s) noexcept
{
    return static_cast<Dest>(checkedCast<std::underlying_type_t<Dest>>(s));
}

template <typename Dest, typename Src>
    requires(std::is_integral_v<Dest> && std::is_enum_v<Src>)
constexpr Dest
checkedCast(Src s) noexcept
{
    return checkedCast<Dest>(std::to_underlying(s));
}
/** @} */

/** Downcast within a class hierarchy, verified in debug builds.

    Performs a static_cast down a hierarchy, but in debug builds verifies
    via dynamic_cast that the object's dynamic type actually permits the
    downcast. Both build modes execute the same conversion; debug builds
    merely add the check.

    The pointer form passes null through unchanged, as dynamic_cast does.

    @note The check requires @p Src to be polymorphic; in release builds
          an invalid downcast is undefined behavior on use, exactly as
          with a bare static_cast.
*/
/** @{ */
template <class Dest, class Src>
    requires(
        std::is_pointer_v<Dest> && std::is_polymorphic_v<Src> &&
        std::derived_from<std::remove_pointer_t<Dest>, Src>)
inline Dest
safeDowncast(Src* s) noexcept
{
    XRPL_ASSERT(s != nullptr, "xrpl::safeDowncast : non-null input");
    XRPL_ASSERT(
        s == nullptr || dynamic_cast<Dest>(s) != nullptr,
        "xrpl::safeDowncast : valid downcast");
    return static_cast<Dest>(s);  // NOLINT(cppcoreguidelines-pro-type-static-cast-downcast)
}

template <class Dest, class Src>
    requires(
        std::is_lvalue_reference_v<Dest> && std::is_polymorphic_v<Src> &&
        std::derived_from<std::remove_reference_t<Dest>, Src>)
inline Dest
safeDowncast(Src& s) noexcept
{
    return *safeDowncast<std::add_pointer_t<std::remove_reference_t<Dest>>>(std::addressof(s));
}
/** @} */

}  // namespace xrpl
