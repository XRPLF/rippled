#pragma once

#include <xrpl/basics/sanitizers.h>
#include <xrpl/beast/type_name.h>

#include <exception>
#include <string>
#include <utility>

namespace xrpl {

/*  Programming By Contract

    This routines are used when checking
    preconditions, postconditions, and invariants.
*/

/** Generates and logs a call stack */
void
LogThrow(std::string const& title);

/** Rethrow the exception currently being handled.

    When called from within a catch block, it will pass
    control to the next matching exception handler, if any.
    Otherwise, std::terminate will be called.

    ASAN can't handle sudden jumps in control flow very well. This
    function is marked as XRPL_NO_SANITIZE_ADDRESS to prevent it from
    triggering false positives, since it throws.
*/
[[noreturn]] XRPL_NO_SANITIZE_ADDRESS inline void
Rethrow()
{
    LogThrow("Re-throwing exception");
    throw;
}

/*
    Logs and throws an exception of type E.

    ASAN can't handle sudden jumps in control flow very well. This
    function is marked as XRPL_NO_SANITIZE_ADDRESS to prevent it from
    triggering false positives, since it throws.
*/

template <class E, class... Args>
[[noreturn]] XRPL_NO_SANITIZE_ADDRESS inline void
Throw(Args&&... args)
{
    static_assert(
        std::is_convertible_v<E*, std::exception*>, "Exception must derive from std::exception.");

    E e(std::forward<Args>(args)...);
    LogThrow(std::string("Throwing exception of type " + beast::type_name<E>() + ": ") + e.what());
    throw std::move(e);
}

/** Called when faulty logic causes a broken invariant. */
[[noreturn]] void
LogicError(std::string const& how) noexcept;

}  // namespace xrpl
