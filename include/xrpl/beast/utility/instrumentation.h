#ifndef BEAST_UTILITY_INSTRUMENTATION_H_INCLUDED
#define BEAST_UTILITY_INSTRUMENTATION_H_INCLUDED

#include <cassert>

#ifdef ENABLE_VOIDSTAR
#ifdef NDEBUG
#error "Antithesis instrumentation requires Debug build"
#endif
#include <antithesis_sdk.h>
#else
// Macros below are copied from antithesis_sdk.h and slightly simplified
// The duplication is because Visual Studio 2019 cannot compile that header
// even with the option -Zc:__cplusplus added.
#define ALWAYS(cond, message, ...) assert((message) && (cond))
#define ALWAYS_OR_UNREACHABLE(cond, message) assert((message) && (cond))
#define SOMETIMES(cond, message, ...)
#define REACHABLE(message, ...)
#define UNREACHABLE(message, ...) assert((message) && false)
#endif

#define XRPL_ASSERT ALWAYS_OR_UNREACHABLE
#define XRPL_ASSERT_PARTS(cond, function, description, ...) \
    XRPL_ASSERT(cond, function " : " description)

// How to use the instrumentation macros:
//
// * XRPL_ASSERT if cond must be true but the line might not be reached during
//   fuzzing. Same like `assert` in normal use.
// * XRPL_ASSERT_PARTS is for convenience, and works like XRPL_ASSERT, but
//   splits the message param into "function" and "description", then joins
//   them with " : " before passing to XRPL_ASSERT.
// * ALWAYS if cond must be true _and_ the line must be reached during fuzzing.
//   Same like `assert` in normal use.
// * REACHABLE if the line must be reached during fuzzing
// * SOMETIMES a hint for the fuzzer to try to make the cond true
// * UNREACHABLE if the line must not be reached (in fuzzing or in normal use).
//   Same like `assert(false)` in normal use.
//
// NOTE: XRPL_ASSERT has similar semantics as C `assert` macro, with only minor
// differences:
// * XRPL_ASSERT must have an unique name (naming convention in CONTRIBUTING.md)
// * during fuzzing, the program will continue execution past failed XRPL_ASSERT
//
// We continue to use regular C `assert` inside unit tests and inside constexpr
// functions.
//
// NOTE: UNREACHABLE does *not* have the same semantics as std::unreachable.
// The program will continue execution past an UNREACHABLE in a Release build
// and during fuzzing (similar to failed XRPL_ASSERT).
// Also, the naming convention in UNREACHABLE is subtly different from other
// instrumentation macros - its name describes the condition which was _not_
// meant to happen, while name in other macros describes the condition that is
// meant to happen (e.g. as in "assert that this happens").

#endif
