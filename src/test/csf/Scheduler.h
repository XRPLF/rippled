//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_CSF_SCHEDULER_H_INCLUDED
#define RIPPLE_TEST_CSF_SCHEDULER_H_INCLUDED

#include <ripple/beast/clock/manual_clock.h>

#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/intrusive/set.hpp>

#include <type_traits>
#include <utility>

namespace ripple {
namespace test {
namespace csf {

/** Simulated discrete-event scheduler.

    Simulates the behavior of events using a single common clock.

    An event is modeled using a lambda function and is scheduled to occur at a
    specific time. Events may be canceled using a token returned when the
    event is scheduled.

    The caller uses one or more of the step, step_one, step_for, step_until and
    step_while functions to process scheduled events.
*/
class Scheduler
{
public:
    using clock_type = beast::manual_clock<std::chrono::steady_clock>;

    using duration = typename clock_type::duration;

    using time_point = typename clock_type::time_point;

private:
    using by_when_hook = boost::intrusive::set_base_hook<
        boost::intrusive::link_mode<boost::intrusive::normal_link>>;

    struct event : by_when_hook
    {
        time_point when;

        event(event const&) = delete;
        event&
        operator=(event const&) = delete;

        virtual ~event() = default;

        // Called to perform the event
        virtual void
        operator()() const = 0;

        event(time_point when_) : when(when_)
        {
        }

        bool
        operator<(event const& other) const
        {
            return when < other.when;
        }
    };

    template <class Handler>
    class event_impl : public event
    {
        Handler const h_;

    public:
        event_impl(event_impl const&) = delete;

        event_impl&
        operator=(event_impl const&) = delete;

        template <class DeducedHandler>
        event_impl(time_point when_, DeducedHandler&& h)
            : event(when_), h_(std::forward<DeducedHandler>(h))
        {
        }

        void
        operator()() const override
        {
            h_();
        }
    };

    class queue_type
    {
    private:
        using by_when_set = typename boost::intrusive::make_multiset<
            event,
            boost::intrusive::constant_time_size<false>>::type;
        // alloc_ is owned by the scheduler
        boost::container::pmr::monotonic_buffer_resource* alloc_;
        by_when_set by_when_;

    public:
        using iterator = typename by_when_set::iterator;

        queue_type(queue_type const&) = delete;
        queue_type&
        operator=(queue_type const&) = delete;

        explicit queue_type(
            boost::container::pmr::monotonic_buffer_resource* alloc);

        ~queue_type();

        bool
        empty() const;

        iterator
        begin();

        iterator
        end();

        template <class Handler>
        typename by_when_set::iterator
        emplace(time_point when, Handler&& h);

        iterator
        erase(iterator iter);
    };

    boost::container::pmr::monotonic_buffer_resource alloc_{256 * 1024};
    queue_type queue_;

    // Aged containers that rely on this clock take a non-const reference =(
    mutable clock_type clock_;

public:
    Scheduler(Scheduler const&) = delete;
    Scheduler&
    operator=(Scheduler const&) = delete;

    Scheduler();

    /** Return the clock. (aged_containers want a non-const ref =( */
    clock_type&
    clock() const;

    /** Return the current network time.

        @note The epoch is unspecified
    */
    time_point
    now() const;

    // Used to cancel timers
    struct cancel_token;

    /** Schedule an event at a specific time

        Effects:

            When the network time is reached,
            the function will be called with
            no arguments.
    */
    template <class Function>
    cancel_token
    at(time_point const& when, Function&& f);

    /** Schedule an event after a specified duration passes

        Effects:

            When the specified time has elapsed,
            the function will be called with
            no arguments.
    */
    template <class Function>
    cancel_token
    in(duration const& delay, Function&& f);

    /** Cancel a timer.

        Preconditions:

            `token` was the return value of a call
            timer() which has not yet been invoked.
    */
    void
    cancel(cancel_token const& token);

    /** Run the scheduler for up to one event.

        Effects:

            The clock is advanced to the time
            of the last delivered event.

        @return `true` if an event was processed.
    */
    bool
    step_one();

    /** Run the scheduler until no events remain.

        Effects:

            The clock is advanced to the time
            of the last event.

        @return `true` if an event was processed.
    */
    bool
    step();

    /** Run the scheduler while a condition is true.

        Function takes no arguments and will be called
        repeatedly after each event is processed to
        decide whether to continue.

        Effects:

            The clock is advanced to the time
            of the last delivered event.

        @return `true` if any event was processed.
    */
    template <class Function>
    bool
    step_while(Function&& func);

    /** Run the scheduler until the specified time.

        Effects:

            The clock is advanced to the
            specified time.

        @return `true` if any event remain.
    */
    bool
    step_until(time_point const& until);

    /** Run the scheduler until time has elapsed.

        Effects:

            The clock is advanced by the
            specified duration.

        @return `true` if any event remain.
    */
    template <class Period, class Rep>
    bool
    step_for(std::chrono::duration<Period, Rep> const& amount);
};

//------------------------------------------------------------------------------

inline Scheduler::queue_type::queue_type(
    boost::container::pmr::monotonic_buffer_resource* alloc)
    : alloc_(alloc)
{
}

inline Scheduler::queue_type::~queue_type()
{
    for (auto iter = by_when_.begin(); iter != by_when_.end();)
    {
        auto e = &*iter;
        ++iter;
        e->~event();
        alloc_->deallocate(e, sizeof(e));
    }
}

inline bool
Scheduler::queue_type::empty() const
{
    return by_when_.empty();
}

inline auto
Scheduler::queue_type::begin() -> iterator
{
    return by_when_.begin();
}

inline auto
Scheduler::queue_type::end() -> iterator
{
    return by_when_.end();
}

template <class Handler>
inline auto
Scheduler::queue_type::emplace(time_point when, Handler&& h) ->
    typename by_when_set::iterator
{
    using event_type = event_impl<std::decay_t<Handler>>;
    auto const p = alloc_->allocate(sizeof(event_type));
    auto& e = *new (p) event_type(when, std::forward<Handler>(h));
    return by_when_.insert(e);
}

inline auto
Scheduler::queue_type::erase(iterator iter) -> typename by_when_set::iterator
{
    auto& e = *iter;
    auto next = by_when_.erase(iter);
    e.~event();
    alloc_->deallocate(&e, sizeof(e));
    return next;
}

//-----------------------------------------------------------------------------
struct Scheduler::cancel_token
{
private:
    typename queue_type::iterator iter_;

public:
    cancel_token() = delete;
    cancel_token(cancel_token const&) = default;
    cancel_token&
    operator=(cancel_token const&) = default;

private:
    friend class Scheduler;
    cancel_token(typename queue_type::iterator iter) : iter_(iter)
    {
    }
};

//------------------------------------------------------------------------------
inline Scheduler::Scheduler() : queue_(&alloc_)
{
}

inline auto
Scheduler::clock() const -> clock_type&
{
    return clock_;
}

inline auto
Scheduler::now() const -> time_point
{
    return clock_.now();
}

template <class Function>
inline auto
Scheduler::at(time_point const& when, Function&& f) -> cancel_token
{
    return queue_.emplace(when, std::forward<Function>(f));
}

template <class Function>
inline auto
Scheduler::in(duration const& delay, Function&& f) -> cancel_token
{
    return at(clock_.now() + delay, std::forward<Function>(f));
}

inline void
Scheduler::cancel(cancel_token const& token)
{
    queue_.erase(token.iter_);
}

inline bool
Scheduler::step_one()
{
    if (queue_.empty())
        return false;
    auto const iter = queue_.begin();
    clock_.set(iter->when);
    (*iter)();
    queue_.erase(iter);
    return true;
}

inline bool
Scheduler::step()
{
    if (!step_one())
        return false;
    for (;;)
        if (!step_one())
            break;
    return true;
}

template <class Function>
inline bool
Scheduler::step_while(Function&& f)
{
    bool ran = false;
    while (f() && step_one())
        ran = true;
    return ran;
}

inline bool
Scheduler::step_until(time_point const& until)
{
    // VFALCO This routine needs optimizing
    if (queue_.empty())
    {
        clock_.set(until);
        return false;
    }
    auto iter = queue_.begin();
    if (iter->when > until)
    {
        clock_.set(until);
        return true;
    }
    do
    {
        step_one();
        iter = queue_.begin();
    } while (iter != queue_.end() && iter->when <= until);
    clock_.set(until);
    return iter != queue_.end();
}

template <class Period, class Rep>
inline bool
Scheduler::step_for(std::chrono::duration<Period, Rep> const& amount)
{
    return step_until(now() + amount);
}

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
