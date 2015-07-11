//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_CONTRACT_H_INCLUDED
#define RIPPLE_BASICS_CONTRACT_H_INCLUDED

#include <exception>
#include <string>
#include <utility>

#ifdef _MSC_VER
# define NORETURN __declspec(noreturn)
#else
# define NORETURN [[ noreturn ]]
#endif

namespace ripple {

/*  Programming By Contract

    This routines are used when checking
    preconditions, postconditions, and invariants.
*/

namespace detail {

NORETURN
void
throwException(
    std::exception_ptr ep);

} // detail

template <class Exception,
    class... Args>
void
Throw (Args&&... args)
{
    throwException(
        std::make_exception_ptr(
            Exception(std::forward<
                Args>(args)...)));
}

/** Called when faulty logic causes a broken invariant. */
NORETURN
void
LogicError (
    std::string const& how);

/** Called when a precondition is not met. */
NORETURN
void
FailPrecondition (std::string const& m);

/** Called to verify a precondition. */
template <class Condition>
void
CheckPrecondition (Condition const& c,
    std::string const& m)
{
    if (static_cast<bool>(c))
        return;
    FailPrecondition(m);
}

#define CHECK_PRECONDITION(c) CheckPrecondition((c), #c)

} // ripple

#endif
