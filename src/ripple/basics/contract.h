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

namespace ripple {

class BasicConfig;

/*  Programming By Contract

    This routines are used when checking
    preconditions, postconditions, and invariants.
*/

namespace detail {

void
throwException(
    std::exception_ptr ep);

} // detail

struct FailureReport
{
    std::string message;
    int line;
    char const* filename;
};

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

/**
   Write a failure report, and then die.
 */
void die(FailureReport const&);

/**
   Write a failure report, and then die if we are in danger mode.
 */
void danger(FailureReport const&);


/** Set or reset danger mode from a BasicConfig. */
void setupDanger(BasicConfig const&);

} // ripple

/**
   Write a failure report, and then die.
 */
#define DIE(MSG) ::ripple::die({(MSG), __LINE__, __FILE__})

/**
   Write a failure report, and then die if we are in danger mode.
 */
#define DANGER(MSG) ::ripple::danger({(MSG), __LINE__, __FILE__})

#define FAILURE_IF(COND, HANDLER)                  \
    do {                                           \
        if (COND)                                  \
            HANDLER({#COND, __LINE__, __FILE__});  \
    } while (false)

/**
   If COND is true, write a failure report, and then die.
 */
#define DANGER_IF(COND) FAILURE_IF((COND), ::ripple::danger)

/**
   If COND is true, write a failure report, and then die if we are in danger
   mode.
 */
#define DIE_IF(COND) FAILURE_IF((COND), ::ripple::die)

/**
   If COND is false, write a failure report, and then die if we are in danger
   mode.
 */
#define DANGER_UNLESS(COND) DANGER_IF(!(COND))

/**
   If COND is false, write a failure report, and then die.
 */
#define DIE_UNLESS(COND) DIE_IF(!(COND))

/**
   DANGER_EXPENSIVE will call the danger function only if the EXPENSIVE
   macro is set and true - or if it's not set and DEBUG is true.
 */

#ifndef EXPENSIVE
    #ifdef DEBUG
        #define EXPENSIVE true
    #else
        #define EXPENSIVE false
    #endif
#endif

#if EXPENSIVE
    #define DANGER_UNLESS_EXPENSIVE(COND) DANGER_UNLESS(COND)
#else
    #define DANGER_UNLESS_EXPENSIVE(COND) do {} while (false)
#endif

#endif
