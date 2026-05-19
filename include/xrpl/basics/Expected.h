#pragma once

#include <xrpl/basics/contract.h>

#include <boost/outcome.hpp>

#include <stdexcept>

namespace xrpl {

/** Expected is an approximation of std::expected (hoped for in C++23)

    See: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0323r10.html

    The implementation is entirely based on boost::outcome_v2::result.
*/

// Exception thrown by an invalid access to Expected.
struct BadExpectedAccess : public std::runtime_error
{
    BadExpectedAccess() : runtime_error("bad expected access")
    {
    }
};

namespace detail {

// Custom policy for Expected.  Always throw on an invalid access.
struct ThrowPolicy : public boost::outcome_v2::policy::base
{
    template <class Impl>
    static constexpr void
    // NOLINTNEXTLINE(readability-identifier-naming)
    wide_value_check(Impl&& self)
    {
        if (!base::_has_value(std::forward<Impl>(self)))
            Throw<BadExpectedAccess>();
    }

    template <class Impl>
    static constexpr void
    // NOLINTNEXTLINE(readability-identifier-naming)
    wide_error_check(Impl&& self)
    {
        if (!base::_has_error(std::forward<Impl>(self)))
            Throw<BadExpectedAccess>();
    }

    template <class Impl>
    static constexpr void
    // NOLINTNEXTLINE(readability-identifier-naming)
    wide_exception_check(Impl&& self)
    {
        if (!base::_has_exception(std::forward<Impl>(self)))
            Throw<BadExpectedAccess>();
    }
};

}  // namespace detail

// Definition of Unexpected, which is used to construct the unexpected
// return type of an Expected.
template <class E>
class Unexpected
{
public:
    static_assert(!std::is_same_v<E, void>, "E must not be void");

    Unexpected() = delete;

    constexpr explicit Unexpected(E const& e) : val_(e)
    {
    }

    constexpr explicit Unexpected(E&& e) : val_(std::move(e))
    {
    }

    [[nodiscard]] constexpr E const&
    value() const&
    {
        return val_;
    }

    constexpr E&
    value() &
    {
        return val_;
    }

    constexpr E&&
    value() &&
    {
        return std::move(val_);
    }

    [[nodiscard]] constexpr E const&&
    value() const&&
    {
        return std::move(val_);
    }

private:
    E val_;
};

// Unexpected deduction guide that converts array to const*.
template <typename E, std::size_t N>
Unexpected(E (&)[N]) -> Unexpected<E const*>;

// Definition of Expected.  All of the machinery comes from boost::result.
template <class T, class E>
class [[nodiscard]] Expected : private boost::outcome_v2::result<T, E, detail::ThrowPolicy>
{
    using Base = boost::outcome_v2::result<T, E, detail::ThrowPolicy>;

public:
    template <typename U>
        requires std::convertible_to<U, T>
    constexpr Expected(U&& r) : Base(boost::outcome_v2::in_place_type_t<T>{}, std::forward<U>(r))
    {
    }

    template <typename U>
        requires std::convertible_to<U, E> && (!std::is_reference_v<U>)
    constexpr Expected(Unexpected<U> e)
        : Base(boost::outcome_v2::in_place_type_t<E>{}, std::move(e.value()))
    {
    }

    [[nodiscard]] constexpr bool
    // NOLINTNEXTLINE(readability-identifier-naming)
    has_value() const
    {
        return Base::has_value();
    }

    [[nodiscard]] constexpr T const&
    value() const
    {
        return Base::value();
    }

    constexpr T&
    value()
    {
        return Base::value();
    }

    [[nodiscard]] constexpr E const&
    error() const&
    {
        return Base::error();
    }

    [[nodiscard]] constexpr E&
    error() &
    {
        return Base::error();
    }

    [[nodiscard]] constexpr E&&
    error() &&
    {
        return std::move(Base::error());
    }

    constexpr explicit
    operator bool() const
    {
        return has_value();
    }

    // Add operator* and operator-> so the Expected API looks a bit more like
    // what std::expected is likely to look like.  See:
    // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0323r10.html
    [[nodiscard]] constexpr T&
    operator*()
    {
        return this->value();
    }

    [[nodiscard]] constexpr T const&
    operator*() const
    {
        return this->value();
    }

    [[nodiscard]] constexpr T*
    operator->()
    {
        return &this->value();
    }

    [[nodiscard]] constexpr T const*
    operator->() const
    {
        return &this->value();
    }
};

// Specialization of Expected<void, E>.  Allows returning either success
// (without a value) or the reason for the failure.
template <class E>
class [[nodiscard]]
Expected<void, E> : private boost::outcome_v2::result<void, E, detail::ThrowPolicy>
{
    using Base = boost::outcome_v2::result<void, E, detail::ThrowPolicy>;

public:
    // The default constructor makes a successful Expected<void, E>.
    // This aligns with std::expected behavior proposed in P0323R10.
    constexpr Expected() : Base(boost::outcome_v2::success())
    {
    }

    template <typename U>
        requires std::convertible_to<U, E> && (!std::is_reference_v<U>)
    constexpr Expected(Unexpected<U> e) : Base(E(std::move(e.value())))
    {
    }

    [[nodiscard]] constexpr E const&
    error() const&
    {
        return Base::error();
    }

    [[nodiscard]] constexpr E&
    error() &
    {
        return Base::error();
    }

    [[nodiscard]] constexpr E&&
    error() &&
    {
        return std::move(Base::error());
    }

    constexpr explicit
    operator bool() const
    {
        return Base::has_value();
    }
};

}  // namespace xrpl
