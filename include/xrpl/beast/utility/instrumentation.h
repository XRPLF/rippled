//------------------------------------------------------------------------------
/*
This file is part of rippled: https://github.com/ripple/rippled
Copyright (c) 2024 Ripple Labs Inc.

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

#ifndef BEAST_UTILITY_INSTRUMENTATION_H_INCLUDED
#define BEAST_UTILITY_INSTRUMENTATION_H_INCLUDED

#include <cassert>

#ifdef ENABLE_VOIDSTAR
#ifdef NDEBUG
#error "Antithesis instrumentation requires Debug build"
#endif
#include <antithesis_sdk.h>
#else
#define ALWAYS(cond, name, ...) assert((name) && (cond))
#define ALWAYS_OR_UNREACHABLE(cond, name, ...) assert((name) && (cond))
#define SOMETIMES(cond, name, ...)
#define REACHABLE(name, ...)
#define UNREACHABLE(name, ...) assert((name) && false)
#endif

#define ASSERT ALWAYS_OR_UNREACHABLE

// How to use the instrumentation macros:
//
// ALWAYS if cond must be true and the line must be reached during fuzzing
// ASSERT if cond must be true but the line might not be reached during fuzzing
// REACHABLE if the line must be reached during fuzzing
// SOMETIMES a hint for the fuzzer to try to make the cond true
// UNREACHABLE if the line must not be reached (in fuzzing or in normal use)
//
// NOTE: ASSERT has similar semantics as C assert macro, with minor differences:
// * ASSERT must have an unique name (naming convention in CONTRIBUTING.md)
// * the condition (which comes first) must be *implicitly* convertible to bool
// * during fuzzing, the program will continue execution past a failed ASSERT
//
// We continue to use regular C assert inside unit tests and inside constexpr
// functions.
//
// NOTE: UNREACHABLE does *not* have the same semantics as std::unreachable.
// The program will continue execution past an UNREACHABLE in a Release build
// and during fuzzing (similar to ASSERT).
// Also, the naming convention in UNREACHABLE is subtly different from other
// instrumentation macros - its name describes the condition which was *not*
// meant to happen, while name in other macros describe the condition that is
// meant to happen (e.g. as in "assert that this happens").

#endif
