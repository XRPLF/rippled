//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CONFIG_CONTRACTCHECKS_H_INCLUDED
#define BEAST_CONFIG_CONTRACTCHECKS_H_INCLUDED

// This file has to work when included in a C source file.

#if defined (fatal_error) || \
    defined (fatal_condition) || \
    defined (meets_condition) || \
    defined (meets_precondition) || \
    defined (meets_postcondition) || \
    defined (meets_invariant) || \
    defined (check_invariant)
#error "Programming by contract macros cannot be overriden!"
#endif

/** Report a fatal error message and terminate the application.
    This macro automatically fills in the file and line number
    Meets this declaration syntax:
    @code inline void fatal_error (char const* message); @endif
    @see FatalError
*/
#define fatal_error(message) beast_reportFatalError (message, __FILE__, __LINE__)

/** Reports a fatal error message type if the condition is false
    The condition is always evaluated regardless of settings.
    Meets this declaration syntax:
    @code inline void fatal_condition (bool condition, char const* category); @endcode
*/
#define fatal_condition(condition,category) static_cast <void> \
    (((!!(condition)) || (beast_reportFatalError ( \
        category " '" BEAST_STRINGIFY(condition) "' failed.", __FILE__, __LINE__), 0)))

/** Reports a fatal error message type if the condition is false
    The condition is always evaluated regardless of settings.
    Meets this declaration syntax:
    @code inline void fatal_condition (bool condition, char const* category); @endcode
*/
#define meets_condition(condition,category) static_cast <bool> \
    (((!!(condition)) || (beast_reportFatalError ( \
        category " '" BEAST_STRINGIFY(condition) "' failed.", __FILE__, __LINE__), false)))

/** Condition tests for programming by contract.
    The condition is always evaluated regardless of settings, and gets returned.
    Meets this declaration syntax:
    @code inline bool meets_condition (bool); @endcode
*/
/** @{ */
#define meets_precondition(condition)  meets_condition(condition,"Pre-condition")
#define meets_postcondition(condition) meets_condition(condition,"Post-condition")
#define meets_invariant(condition)     meets_condition(condition,"Invariant")
/** @} */

/** Condition tests for programming by contract.
    The condition is evaluated only if BEAST_DISABLE_CONTRACT_CHECKS is 0.
    Meets this declaration syntax:
    @code inline void check_condition (bool); @endcode
    @see BEAST_DISABLE_CONTRACT_CHECKS
*/
/** @{ */
#if ! BEAST_DISABLE_CONTRACT_CHECKS
# define check_invariant(condition)     meets_invariant(condition)
#else
# define check_invariant(condition)     ((void)0)
#endif
/** @} */

#endif
