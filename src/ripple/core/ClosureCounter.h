//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_CLOSURE_COUNTER_H_INCLUDED
#define RIPPLE_CORE_CLOSURE_COUNTER_H_INCLUDED

#include <ripple/basics/Log.h>
#include <boost/optional.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <type_traits>

namespace ripple {

/**
 * The role of a `ClosureCounter` is to assist in shutdown by letting callers
 * wait for the completion of callbacks (of a single type signature) that they
 * previously scheduled. The lifetime of a `ClosureCounter` consists of two
 * phases: the initial expanding "fork" phase, and the subsequent shrinking
 * "join" phase.
 *
 * In the fork phase, callers register a callback by passing the callback and
 * receiving a substitute in return. The substitute has the same callable
 * interface as the callback, and it informs the `ClosureCounter` whenever it
 * is copied or destroyed, so that it can keep an accurate count of copies.
 *
 * The transition to the join phase is made by a call to `join`. In this
 * phase, every substitute returned going forward will be empty, signaling to
 * the caller that they should just drop the callback and cancel their
 * asynchronous operation. `join` blocks until all existing callback
 * substitutes are destroyed.
 *
 * \tparam Ret_t The return type of the closure.
 * \tparam Args_t The argument types of the closure.
 */
template <typename Ret_t, typename... Args_t>
class ClosureCounter
{
private:
    std::mutex mutable mutex_{};
    std::condition_variable allClosuresDoneCond_{};  // guard with mutex_
    bool waitForClosures_{false};                    // guard with mutex_
    std::atomic<int> closureCount_{0};

    // Increment the count.
    ClosureCounter&
    operator++()
    {
        ++closureCount_;
        return *this;
    }

    // Decrement the count.  If we're stopping and the count drops to zero
    // notify allClosuresDoneCond_.
    ClosureCounter&
    operator--()
    {
        // Even though closureCount_ is atomic, we decrement its value under
        // a lock.  This removes a small timing window that occurs if the
        // waiting thread is handling a spurious wakeup when closureCount_
        // drops to zero.
        std::lock_guard lock{mutex_};

        // Update closureCount_.  Notify if stopping and closureCount_ == 0.
        if ((--closureCount_ == 0) && waitForClosures_)
            allClosuresDoneCond_.notify_all();
        return *this;
    }

    // A private template class that helps count the number of closures
    // in flight. This allows callers to block until all their postponed
    // closures are dispatched.
    template <typename Closure>
    class Substitute
    {
    private:
        ClosureCounter& counter_;
        std::remove_reference_t<Closure> closure_;

        static_assert(
            std::is_same<decltype(closure_(std::declval<Args_t>()...)), Ret_t>::
                value,
            "Closure arguments don't match ClosureCounter Ret_t or Args_t");

    public:
        Substitute() = delete;

        Substitute(Substitute const& rhs)
            : counter_(rhs.counter_), closure_(rhs.closure_)
        {
            ++counter_;
        }

        Substitute(Substitute&& rhs) noexcept(
            std::is_nothrow_move_constructible<Closure>::value)
            : counter_(rhs.counter_), closure_(std::move(rhs.closure_))
        {
            ++counter_;
        }

        Substitute(ClosureCounter& counter, Closure&& closure)
            : counter_(counter), closure_(std::forward<Closure>(closure))
        {
            ++counter_;
        }

        Substitute&
        operator=(Substitute const& rhs) = delete;
        Substitute&
        operator=(Substitute&& rhs) = delete;

        ~Substitute()
        {
            --counter_;
        }

        // Note that Args_t is not deduced, it is explicit.  So Args_t&&
        // would be an rvalue reference, not a forwarding reference.  We
        // want to forward exactly what the user declared.
        Ret_t
        operator()(Args_t... args)
        {
            return closure_(std::forward<Args_t>(args)...);
        }
    };

public:
    ClosureCounter() = default;
    // Not copyable or movable.  Outstanding counts would be hard to sort out.
    ClosureCounter(ClosureCounter const&) = delete;

    ClosureCounter&
    operator=(ClosureCounter const&) = delete;

    /** Destructor verifies all in-flight closures are complete. */
    ~ClosureCounter()
    {
        using namespace std::chrono_literals;
        join("ClosureCounter", 1s, debugLog());
    }

    /** Returns once all counted in-flight closures are destroyed.

        @param name Name reported if join time exceeds wait.
        @param wait If join() exceeds this duration report to Journal.
        @param j Journal written to if wait is exceeded.
     */
    void
    join(char const* name, std::chrono::milliseconds wait, beast::Journal j)
    {
        std::unique_lock<std::mutex> lock{mutex_};
        waitForClosures_ = true;
        if (closureCount_ > 0)
        {
            if (!allClosuresDoneCond_.wait_for(
                    lock, wait, [this] { return closureCount_ == 0; }))
            {
                if (auto stream = j.error())
                    stream << name << " waiting for ClosureCounter::join().";
                allClosuresDoneCond_.wait(
                    lock, [this] { return closureCount_ == 0; });
            }
        }
    }

    /** Wrap the passed closure with a reference counter.

        @param closure Closure that accepts Args_t parameters and returns Ret_t.
        @return If join() has been called returns boost::none.  Otherwise
                returns a boost::optional that wraps closure with a
                reference counter.
    */
    template <class Closure>
    boost::optional<Substitute<Closure>>
    wrap(Closure&& closure)
    {
        boost::optional<Substitute<Closure>> ret;

        std::lock_guard lock{mutex_};
        if (!waitForClosures_)
            ret.emplace(*this, std::forward<Closure>(closure));

        return ret;
    }

    /** Current number of Closures outstanding.  Only useful for testing. */
    int
    count() const
    {
        return closureCount_;
    }

    /** Returns true if this has been joined.

        Even if true is returned, counted closures may still be in flight.
        However if (joined() && (count() == 0)) there should be no more
        counted closures in flight.
    */
    bool
    joined() const
    {
        std::lock_guard lock{mutex_};
        return waitForClosures_;
    }
};

}  // namespace ripple

#endif  // RIPPLE_CORE_CLOSURE_COUNTER_H_INCLUDED
