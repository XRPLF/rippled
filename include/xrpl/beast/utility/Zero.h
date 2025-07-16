//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Tom Ritchford <tom@swirly.com>

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

#ifndef BEAST_UTILITY_ZERO_H_INCLUDED
#define BEAST_UTILITY_ZERO_H_INCLUDED

namespace beast {

/** Zero allows classes to offer efficient comparisons to zero.

    Zero is a struct to allow classes to efficiently compare with zero without
    requiring an rvalue construction.

    It's often the case that we have classes which combine a number and a unit.
    In such cases, comparisons like t > 0 or t != 0 make sense, but comparisons
    like t > 1 or t != 1 do not.

    The class Zero allows such comparisons to be easily made.

    The comparing class T either needs to have a method called signum() which
    returns a positive number, 0, or a negative; or there needs to be a signum
    function which resolves in the namespace which takes an instance of T and
    returns a positive, zero or negative number.
*/

struct Zero
{
    explicit Zero() = default;
};

namespace {
static constexpr Zero zero{};
}

/** Default implementation of signum calls the method on the class. */
template <typename T>
auto
signum(T const& t)
{
    return t.signum();
}

namespace detail {
namespace zero_helper {

// For argument dependent lookup to function properly, calls to signum must
// be made from a namespace that does not include overloads of the function..
template <class T>
auto
call_signum(T const& t)
{
    return signum(t);
}

}  // namespace zero_helper
}  // namespace detail

// Handle operators where T is on the left side using signum.

template <typename T>
bool
operator==(T const& t, Zero)
{
    return detail::zero_helper::call_signum(t) == 0;
}

template <typename T>
bool
operator!=(T const& t, Zero)
{
    return detail::zero_helper::call_signum(t) != 0;
}

template <typename T>
bool
operator<(T const& t, Zero)
{
    return detail::zero_helper::call_signum(t) < 0;
}

template <typename T>
bool
operator>(T const& t, Zero)
{
    return detail::zero_helper::call_signum(t) > 0;
}

template <typename T>
bool
operator>=(T const& t, Zero)
{
    return detail::zero_helper::call_signum(t) >= 0;
}

template <typename T>
bool
operator<=(T const& t, Zero)
{
    return detail::zero_helper::call_signum(t) <= 0;
}

// Handle operators where T is on the right side by
// reversing the operation, so that T is on the left side.

template <typename T>
bool
operator==(Zero, T const& t)
{
    return t == zero;
}

template <typename T>
bool
operator!=(Zero, T const& t)
{
    return t != zero;
}

template <typename T>
bool
operator<(Zero, T const& t)
{
    return t > zero;
}

template <typename T>
bool
operator>(Zero, T const& t)
{
    return t < zero;
}

template <typename T>
bool
operator>=(Zero, T const& t)
{
    return t <= zero;
}

template <typename T>
bool
operator<=(Zero, T const& t)
{
    return t >= zero;
}

}  // namespace beast

#endif
