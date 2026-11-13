#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace xrpl {

/**
 * Opt-in bitwise operators for scoped enumerations.
 *
 * Provides `&`, `|`, `^`, `~` and the corresponding compound assignment forms
 * for any scoped enumeration with an unsigned underlying type that opts in by
 * specializing @ref OptIn:
 *
 * @code
 * namespace xrpl {
 *
 * enum class MyFlags : std::uint32_t { a = 1, b = 2, c = 4 };
 *
 * template <>
 * struct enum_bitops::OptIn<MyFlags> : std::true_type {};
 *
 * }  // namespace xrpl
 * @endcode
 *
 * The operators are constexpr and noexcept, and return the enumeration type.
 *
 * @par Where the specialization may be declared
 *   In namespace xrpl, or at global scope with full qualification, and after
 *   the enumeration is defined but before the operators are first used. This
 *   cannot be done in a nested namespace or at class scope; for enumerations
 *   nested in a class this means after the class definition.
 *
 * @par Where the operators are found
 *   The operators are declared in namespace xrpl, so argument-dependent lookup
 *   will find them only for enumerations declared in xrpl or nested in a class
 *   declared directly in xrpl. Enumerations in a nested namespace will only be
 *   found from code inside xrpl, and only if no enclosing scope declares an
 *   operator of the same name; elsewhere they require using-declarations.
 */
namespace enum_bitops {

/**
 * Types that may be opted in to the bitwise operators.
 *
 * Satisfied by a scoped enumeration whose underlying type is an unsigned
 * integer type.
 *
 * @tparam T The type to test.
 */
template <typename T>
concept Eligible = std::is_scoped_enum_v<T> && std::unsigned_integral<std::underlying_type_t<T>>;

/**
 * Opt-in switch for the bitwise operators.
 *
 * The primary template derives from std::false_type. Specialize it to derive
 * from std::true_type to enable the operators for an @ref Eligible enumeration.
 *
 * @tparam T The scoped enumeration to opt in.
 */
template <Eligible T>
struct OptIn : std::false_type
{
};

/**
 * Enumerations for which the bitwise operators are enabled.
 *
 * Satisfied when @p T satisfies @ref Eligible and @ref OptIn has been
 * specialized for it to derive from std::true_type.
 *
 * @tparam T The type to test.
 */
template <typename T>
concept Candidate = Eligible<T> && OptIn<T>::value;

}  // namespace enum_bitops

// These are declared in xrpl, not in enum_bitops, so that argument-dependent
// lookup finds them for enumerations whose associated namespace is xrpl.

/**
 * @name Bitwise operators for opted-in scoped enumerations
 *
 * Each operator applies the corresponding built-in operator to the
 * underlying values and converts the result back to the enumeration type.
 * Both operands must have the same enumeration type; there is no implicit
 * conversion to or from the underlying type.
 */
/** @{ */

/**
 * Bitwise AND.
 *
 * @param lhs The left operand.
 * @param rhs The right operand.
 * @return The bits set in both @p lhs and @p rhs.
 */
template <enum_bitops::Candidate T>
constexpr T
operator&(T lhs, T rhs) noexcept
{
    return static_cast<T>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

/**
 * Bitwise OR.
 *
 * @param lhs The left operand.
 * @param rhs The right operand.
 * @return The bits set in @p lhs, in @p rhs, or in both.
 */
template <enum_bitops::Candidate T>
constexpr T
operator|(T lhs, T rhs) noexcept
{
    return static_cast<T>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

/**
 * Bitwise exclusive OR.
 *
 * @param lhs The left operand.
 * @param rhs The right operand.
 * @return The bits set in exactly one of @p lhs and @p rhs.
 */
template <enum_bitops::Candidate T>
constexpr T
operator^(T lhs, T rhs) noexcept
{
    return static_cast<T>(std::to_underlying(lhs) ^ std::to_underlying(rhs));
}

/**
 * Bitwise complement.
 *
 * The result has every bit of the underlying type that is clear in
 * @p val, including bits that no enumerator names. It is intended for
 * clearing flags, as in `flags & ~flag`.
 *
 * @param val The operand.
 * @return The complement of @p val.
 */
template <enum_bitops::Candidate T>
constexpr T
operator~(T val) noexcept
{
    return static_cast<T>(~std::to_underlying(val));
}

/**
 * Bitwise AND assignment.
 *
 * @param lhs The value to modify.
 * @param rhs The right operand.
 * @return A reference to @p lhs.
 */
template <enum_bitops::Candidate T>
constexpr T&
operator&=(T& lhs, T rhs) noexcept
{
    lhs = lhs & rhs;
    return lhs;
}

/**
 * Bitwise OR assignment.
 *
 * @param lhs The value to modify.
 * @param rhs The right operand.
 * @return A reference to @p lhs.
 */
template <enum_bitops::Candidate T>
constexpr T&
operator|=(T& lhs, T rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

/**
 * Bitwise exclusive OR assignment.
 *
 * @param lhs The value to modify.
 * @param rhs The right operand.
 * @return A reference to @p lhs.
 */
template <enum_bitops::Candidate T>
constexpr T&
operator^=(T& lhs, T rhs) noexcept
{
    lhs = lhs ^ rhs;
    return lhs;
}

/** @} */

}  // namespace xrpl
