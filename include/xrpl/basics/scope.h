//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2021 Ripple Inc.

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

#ifndef RIPPLE_BASICS_SCOPE_H_INCLUDED
#define RIPPLE_BASICS_SCOPE_H_INCLUDED

#include <exception>
#include <mutex>
#include <type_traits>
#include <utility>

namespace ripple {

// RAII scope helpers.  As specified in Library Fundamental, Version 3
// Basic design of idea:  https://www.youtube.com/watch?v=WjTrfoiB0MQ
// Specification:
//   http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/n4873.html#scopeguard

// This implementation deviates from the spec slightly:
// The scope_exit and scope_fail constructors taking a functor are not
// permitted to throw an exception.  This was done because some compilers
// did not like the superfluous try/catch in the common instantiations
// where the construction was noexcept.  Instead a static_assert is used
// to enforce this restriction.

template <class EF>
class scope_exit
{
    EF exit_function_;
    bool execute_on_destruction_{true};

public:
    ~scope_exit()
    {
        if (execute_on_destruction_)
            exit_function_();
    }

    scope_exit(scope_exit&& rhs) noexcept(
        std::is_nothrow_move_constructible_v<EF> ||
        std::is_nothrow_copy_constructible_v<EF>)
        : exit_function_{std::forward<EF>(rhs.exit_function_)}
        , execute_on_destruction_{rhs.execute_on_destruction_}
    {
        rhs.release();
    }

    scope_exit&
    operator=(scope_exit&&) = delete;

    template <class EFP>
    explicit scope_exit(
        EFP&& f,
        std::enable_if_t<
            !std::is_same_v<std::remove_cv_t<EFP>, scope_exit> &&
            std::is_constructible_v<EF, EFP>>* = 0) noexcept
        : exit_function_{std::forward<EFP>(f)}
    {
        static_assert(
            std::
                is_nothrow_constructible_v<EF, decltype(std::forward<EFP>(f))>);
    }

    void
    release() noexcept
    {
        execute_on_destruction_ = false;
    }
};

template <class EF>
scope_exit(EF) -> scope_exit<EF>;

template <class EF>
class scope_fail
{
    EF exit_function_;
    bool execute_on_destruction_{true};
    int uncaught_on_creation_{std::uncaught_exceptions()};

public:
    ~scope_fail()
    {
        if (execute_on_destruction_ &&
            std::uncaught_exceptions() > uncaught_on_creation_)
            exit_function_();
    }

    scope_fail(scope_fail&& rhs) noexcept(
        std::is_nothrow_move_constructible_v<EF> ||
        std::is_nothrow_copy_constructible_v<EF>)
        : exit_function_{std::forward<EF>(rhs.exit_function_)}
        , execute_on_destruction_{rhs.execute_on_destruction_}
        , uncaught_on_creation_{rhs.uncaught_on_creation_}
    {
        rhs.release();
    }

    scope_fail&
    operator=(scope_fail&&) = delete;

    template <class EFP>
    explicit scope_fail(
        EFP&& f,
        std::enable_if_t<
            !std::is_same_v<std::remove_cv_t<EFP>, scope_fail> &&
            std::is_constructible_v<EF, EFP>>* = 0) noexcept
        : exit_function_{std::forward<EFP>(f)}
    {
        static_assert(
            std::
                is_nothrow_constructible_v<EF, decltype(std::forward<EFP>(f))>);
    }

    void
    release() noexcept
    {
        execute_on_destruction_ = false;
    }
};

template <class EF>
scope_fail(EF) -> scope_fail<EF>;

template <class EF>
class scope_success
{
    EF exit_function_;
    bool execute_on_destruction_{true};
    int uncaught_on_creation_{std::uncaught_exceptions()};

public:
    ~scope_success() noexcept(noexcept(exit_function_()))
    {
        if (execute_on_destruction_ &&
            std::uncaught_exceptions() <= uncaught_on_creation_)
            exit_function_();
    }

    scope_success(scope_success&& rhs) noexcept(
        std::is_nothrow_move_constructible_v<EF> ||
        std::is_nothrow_copy_constructible_v<EF>)
        : exit_function_{std::forward<EF>(rhs.exit_function_)}
        , execute_on_destruction_{rhs.execute_on_destruction_}
        , uncaught_on_creation_{rhs.uncaught_on_creation_}
    {
        rhs.release();
    }

    scope_success&
    operator=(scope_success&&) = delete;

    template <class EFP>
    explicit scope_success(
        EFP&& f,
        std::enable_if_t<
            !std::is_same_v<std::remove_cv_t<EFP>, scope_success> &&
            std::is_constructible_v<EF, EFP>>* =
            0) noexcept(std::is_nothrow_constructible_v<EF, EFP> || std::is_nothrow_constructible_v<EF, EFP&>)
        : exit_function_{std::forward<EFP>(f)}
    {
    }

    void
    release() noexcept
    {
        execute_on_destruction_ = false;
    }
};

template <class EF>
scope_success(EF) -> scope_success<EF>;

/**
    Automatically unlocks and re-locks a unique_lock object.

    This is the reverse of a std::unique_lock object - instead of locking the
   mutex for the lifetime of this object, it unlocks it.

    Make sure you don't try to unlock mutexes that aren't actually locked!

    This is essentially a less-versatile boost::reverse_lock.

    e.g. @code

    std::mutex mut;

    for (;;)
    {
        std::unique_lock myScopedLock{mut};
        // mut is now locked

        ... do some stuff with it locked ..

        while (xyz)
        {
            ... do some stuff with it locked ..

            scope_unlock unlocker{myScopedLock};

            // mut is now unlocked for the remainder of this block,
            // and re-locked at the end.

            ...do some stuff with it unlocked ...
        }  // mut gets locked here.

    }  // mut gets unlocked here
    @endcode
*/

template <class Mutex>
class scope_unlock
{
    std::unique_lock<Mutex>* plock;

public:
    explicit scope_unlock(std::unique_lock<Mutex>& lock) noexcept(true)
        : plock(&lock)
    {
        assert(plock->owns_lock());
        plock->unlock();
    }

    // Immovable type
    scope_unlock(scope_unlock const&) = delete;
    scope_unlock&
    operator=(scope_unlock const&) = delete;

    ~scope_unlock() noexcept(true)
    {
        plock->lock();
    }
};

template <class Mutex>
scope_unlock(std::unique_lock<Mutex>&) -> scope_unlock<Mutex>;

}  // namespace ripple

#endif
