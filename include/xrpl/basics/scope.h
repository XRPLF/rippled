#pragma once

#include <xrpl/beast/utility/instrumentation.h>

#include <exception>
#include <mutex>
#include <type_traits>
#include <utility>

namespace xrpl {

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
class ScopeExit
{
    EF exit_function_;
    bool execute_on_destruction_{true};

public:
    ~ScopeExit()
    {
        if (execute_on_destruction_)
            exit_function_();
    }

    ScopeExit(ScopeExit&& rhs) noexcept(
        std::is_nothrow_move_constructible_v<EF> || std::is_nothrow_copy_constructible_v<EF>)
        : exit_function_{std::forward<EF>(rhs.exit_function_)}
        , execute_on_destruction_{rhs.execute_on_destruction_}
    {
        rhs.release();
    }

    ScopeExit&
    operator=(ScopeExit&&) = delete;

    template <class EFP>
    explicit ScopeExit(
        EFP&& f,
        std::enable_if_t<
            !std::is_same_v<std::remove_cv_t<EFP>, ScopeExit> &&
            std::is_constructible_v<EF, EFP>>* = 0) noexcept
        : exit_function_{std::forward<EFP>(f)}
    {
        static_assert(std::is_nothrow_constructible_v<EF, decltype(std::forward<EFP>(f))>);
    }

    void
    release() noexcept
    {
        execute_on_destruction_ = false;
    }
};

template <class EF>
ScopeExit(EF) -> ScopeExit<EF>;

template <class EF>
class ScopeFail
{
    EF exit_function_;
    bool execute_on_destruction_{true};
    int uncaught_on_creation_{std::uncaught_exceptions()};

public:
    ~ScopeFail()
    {
        if (execute_on_destruction_ && std::uncaught_exceptions() > uncaught_on_creation_)
            exit_function_();
    }

    ScopeFail(ScopeFail&& rhs) noexcept(
        std::is_nothrow_move_constructible_v<EF> || std::is_nothrow_copy_constructible_v<EF>)
        : exit_function_{std::forward<EF>(rhs.exit_function_)}
        , execute_on_destruction_{rhs.execute_on_destruction_}
        , uncaught_on_creation_{rhs.uncaught_on_creation_}
    {
        rhs.release();
    }

    ScopeFail&
    operator=(ScopeFail&&) = delete;

    template <class EFP>
    explicit ScopeFail(
        EFP&& f,
        std::enable_if_t<
            !std::is_same_v<std::remove_cv_t<EFP>, ScopeFail> &&
            std::is_constructible_v<EF, EFP>>* = 0) noexcept
        : exit_function_{std::forward<EFP>(f)}
    {
        static_assert(std::is_nothrow_constructible_v<EF, decltype(std::forward<EFP>(f))>);
    }

    void
    release() noexcept
    {
        execute_on_destruction_ = false;
    }
};

template <class EF>
ScopeFail(EF) -> ScopeFail<EF>;

template <class EF>
class ScopeSuccess
{
    EF exit_function_;
    bool execute_on_destruction_{true};
    int uncaught_on_creation_{std::uncaught_exceptions()};

public:
    ~ScopeSuccess() noexcept(noexcept(exit_function_()))
    {
        if (execute_on_destruction_ && std::uncaught_exceptions() <= uncaught_on_creation_)
            exit_function_();
    }

    ScopeSuccess(ScopeSuccess&& rhs) noexcept(
        std::is_nothrow_move_constructible_v<EF> || std::is_nothrow_copy_constructible_v<EF>)
        : exit_function_{std::forward<EF>(rhs.exit_function_)}
        , execute_on_destruction_{rhs.execute_on_destruction_}
        , uncaught_on_creation_{rhs.uncaught_on_creation_}
    {
        rhs.release();
    }

    ScopeSuccess&
    operator=(ScopeSuccess&&) = delete;

    template <class EFP>
    explicit ScopeSuccess(
        EFP&& f,
        std::enable_if_t<
            !std::is_same_v<std::remove_cv_t<EFP>, ScopeSuccess> &&
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
ScopeSuccess(EF) -> ScopeSuccess<EF>;

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
class ScopeUnlock
{
    std::unique_lock<Mutex>* plock_;

public:
    explicit ScopeUnlock(std::unique_lock<Mutex>& lock) noexcept(true) : plock_(&lock)
    {
        XRPL_ASSERT(plock_->owns_lock(), "xrpl::ScopeUnlock::ScopeUnlock : mutex must be locked");
        plock_->unlock();
    }

    // Immovable type
    ScopeUnlock(ScopeUnlock const&) = delete;
    ScopeUnlock&
    operator=(ScopeUnlock const&) = delete;

    ~ScopeUnlock() noexcept(true)
    {
        plock_->lock();
    }
};

template <class Mutex>
ScopeUnlock(std::unique_lock<Mutex>&) -> ScopeUnlock<Mutex>;

}  // namespace xrpl
