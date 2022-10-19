//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_BASICS_EXPECTED_H_INCLUDED
#define RIPPLE_BASICS_EXPECTED_H_INCLUDED

#include <ripple/basics/contract.h>

#include <boost/outcome.hpp>

#include <concepts>
#include <stdexcept>
#include <type_traits>

namespace ripple {

/** Expected is an approximation of std::expected (hoped for in C++23)

    See: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0323r10.html

    The implementation is entirely based on boost::outcome_v2::result.
*/

// Exception thrown by an invalid access to Expected.
struct bad_expected_access : public std::runtime_error
{
    bad_expected_access() : runtime_error("bad expected access")
    {
    }
};

namespace detail {

// Custom policy for Expected.  Always throw on an invalid access.
struct throw_policy : public boost::outcome_v2::policy::base
{
    template <class Impl>
    static constexpr void
    wide_value_check(Impl&& self)
    {
        if (!base::_has_value(std::forward<Impl>(self)))
            Throw<bad_expected_access>();
    }

    template <class Impl>
    static constexpr void
    wide_error_check(Impl&& self)
    {
        if (!base::_has_error(std::forward<Impl>(self)))
            Throw<bad_expected_access>();
    }

    template <class Impl>
    static constexpr void
    wide_exception_check(Impl&& self)
    {
        if (!base::_has_exception(std::forward<Impl>(self)))
            Throw<bad_expected_access>();
    }
};

}  // namespace detail

// Definition of Unexpected, which is used to construct the unexpected
// return type of an Expected.
template <class E>
class Unexpected
{
public:
    static_assert(!std::is_same<E, void>::value, "E must not be void");

    Unexpected() = delete;

    constexpr explicit Unexpected(E const& e) : val_(e)
    {
    }

    constexpr explicit Unexpected(E&& e) : val_(std::move(e))
    {
    }

    constexpr const E&
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

    constexpr const E&&
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
class [[nodiscard]] Expected
    : private boost::outcome_v2::result<T, E, detail::throw_policy>
{
    using Base = boost::outcome_v2::result<T, E, detail::throw_policy>;

public:
    template <typename U>
    requires std::convertible_to<U, T> constexpr Expected(U && r)
        : Base(T{std::forward<U>(r)})
    {
    }

    template <typename U>
        requires std::convertible_to<U, E> &&
        (!std::is_reference_v<U>)constexpr Expected(Unexpected<U> e)
        : Base(E{std::move(e.value())})
    {
    }

    constexpr bool has_value() const
    {
        return Base::has_value();
    }

    constexpr T const& value() const
    {
        return Base::value();
    }

    constexpr T& value()
    {
        return Base::value();
    }

    constexpr E const& error() const
    {
        return Base::error();
    }

    constexpr E& error()
    {
        return Base::error();
    }

    constexpr explicit operator bool() const
    {
        return has_value();
    }

    // Add operator* and operator-> so the Expected API looks a bit more like
    // what std::expected is likely to look like.  See:
    // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0323r10.html
    [[nodiscard]] constexpr T& operator*()
    {
        return this->value();
    }

    [[nodiscard]] constexpr T const& operator*() const
    {
        return this->value();
    }

    [[nodiscard]] constexpr T* operator->()
    {
        return &this->value();
    }

    [[nodiscard]] constexpr T const* operator->() const
    {
        return &this->value();
    }
};

// Specialization of Expected<void, E>.  Allows returning either success
// (without a value) or the reason for the failure.
template <class E>
class [[nodiscard]] Expected<void, E>
    : private boost::outcome_v2::result<void, E, detail::throw_policy>
{
    using Base = boost::outcome_v2::result<void, E, detail::throw_policy>;

public:
    // The default constructor makes a successful Expected<void, E>.
    // This aligns with std::expected behavior proposed in P0323R10.
    constexpr Expected() : Base(boost::outcome_v2::success())
    {
    }

    template <typename U>
        requires std::convertible_to<U, E> &&
        (!std::is_reference_v<U>)constexpr Expected(Unexpected<U> e)
        : Base(E{std::move(e.value())})
    {
    }

    constexpr E const& error() const
    {
        return Base::error();
    }

    constexpr E& error()
    {
        return Base::error();
    }

    constexpr explicit operator bool() const
    {
        return Base::has_value();
    }
};

}  // namespace ripple

#endif  // RIPPLE_BASICS_EXPECTED_H_INCLUDED
