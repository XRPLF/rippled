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

#include <ripple/beast/type_name.h>
#include <exception>
#include <string>
#include <typeinfo>
#include <utility>

namespace ripple {

/*  Programming By Contract

    This routines are used when checking
    preconditions, postconditions, and invariants.
*/

/** Generates and logs a call stack */
void
LogThrow (std::string const& title);

/** Rethrow the exception currently being handled.

    When called from within a catch block, it will pass
    control to the next matching exception handler, if any.
    Otherwise, std::terminate will be called.
*/
[[noreturn]]
inline
void
Rethrow ()
{
    LogThrow ("Re-throwing exception");
    throw;
}

template <class E, class... Args>
[[noreturn]]
inline
void
Throw (Args&&... args)
{
    static_assert (std::is_convertible<E*, std::exception*>::value,
        "Exception must derive from std::exception.");

    E e(std::forward<Args>(args)...);
    LogThrow (std::string("Throwing exception of type " +
                          beast::type_name<E>() +": ") + e.what());
    throw e;
}

/** Called when faulty logic causes a broken invariant. */
[[noreturn]]
void
LogicError (
    std::string const& how) noexcept;

} // ripple

#endif
