#pragma once

#include <xrpl/basics/ByteUtilities.h>
#include <xrpl/beast/clock/manual_clock.h>

#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/intrusive/set.hpp>

#include <type_traits>
#include <utility>

namespace xrpl::test::csf {

/** Simulated discrete-event scheduler.

    Simulates the behavior of events using a single common clock.

    An event is modeled using a lambda function and is scheduled to occur at a
    specific time. Events may be canceled using a token returned when the
    event is scheduled.

    The caller uses one or more of the step, stepOne, stepFor, stepUntil and
    stepWhile functions to process scheduled events.
*/
class Scheduler
{
public:
    using clock_type = beast::ManualClock<std::chrono::steady_clock>;

    using duration = typename clock_type::duration;

    using time_point = typename clock_type::time_point;

private:
    using by_when_hook =
        boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>;

    struct Event : by_when_hook
    {
        time_point when;

        Event(Event const&) = delete;
        Event&
        operator=(Event const&) = delete;

        virtual ~Event() = default;

        // Called to perform the event
        virtual void
        operator()() const = 0;

        Event(time_point when) : when(when)
        {
        }

        bool
        operator<(Event const& other) const
        {
            return when < other.when;
        }
    };

    template <class Handler>
    class EventImpl : public Event
    {
        Handler const h_;

    public:
        EventImpl(EventImpl const&) = delete;

        EventImpl&
        operator=(EventImpl const&) = delete;

        template <class DeducedHandler>
        EventImpl(time_point when, DeducedHandler&& h)
            : Event(when), h_(std::forward<DeducedHandler>(h))
        {
        }

        void
        operator()() const override
        {
            h_();
        }
    };

    class QueueType
    {
    private:
        using by_when_set = typename boost::intrusive::
            make_multiset<Event, boost::intrusive::constant_time_size<false>>::type;
        // alloc_ is owned by the scheduler
        boost::container::pmr::monotonic_buffer_resource* alloc_;
        by_when_set byWhen_;

    public:
        using iterator = typename by_when_set::iterator;

        QueueType(QueueType const&) = delete;
        QueueType&
        operator=(QueueType const&) = delete;

        explicit QueueType(boost::container::pmr::monotonic_buffer_resource* alloc);

        ~QueueType();

        [[nodiscard]] bool
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

    boost::container::pmr::monotonic_buffer_resource alloc_{kilobytes(256)};
    QueueType queue_;

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
    struct CancelToken;

    /** Schedule an event at a specific time

        Effects:

            When the network time is reached,
            the function will be called with
            no arguments.
    */
    template <class Function>
    CancelToken
    at(time_point const& when, Function&& f);

    /** Schedule an event after a specified duration passes

        Effects:

            When the specified time has elapsed,
            the function will be called with
            no arguments.
    */
    template <class Function>
    CancelToken
    in(duration const& delay, Function&& f);

    /** Cancel a timer.

        Preconditions:

            `token` was the return value of a call
            timer() which has not yet been invoked.
    */
    void
    cancel(CancelToken const& token);

    /** Run the scheduler for up to one event.

        Effects:

            The clock is advanced to the time
            of the last delivered event.

        @return `true` if an event was processed.
    */
    bool
    stepOne();

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
    stepWhile(Function&& func);

    /** Run the scheduler until the specified time.

        Effects:

            The clock is advanced to the
            specified time.

        @return `true` if any event remain.
    */
    bool
    stepUntil(time_point const& until);

    /** Run the scheduler until time has elapsed.

        Effects:

            The clock is advanced by the
            specified duration.

        @return `true` if any event remain.
    */
    template <class Period, class Rep>
    bool
    stepFor(std::chrono::duration<Period, Rep> const& amount);
};

//------------------------------------------------------------------------------

inline Scheduler::QueueType::QueueType(boost::container::pmr::monotonic_buffer_resource* alloc)
    : alloc_(alloc)
{
}

inline Scheduler::QueueType::~QueueType()
{
    for (auto iter = byWhen_.begin(); iter != byWhen_.end();)
    {
        auto e = &*iter;
        ++iter;
        e->~Event();
        alloc_->deallocate(e, sizeof(e));  // NOLINT(bugprone-sizeof-expression)
    }
}

inline bool
Scheduler::QueueType::empty() const
{
    return byWhen_.empty();
}

inline auto
Scheduler::QueueType::begin() -> iterator
{
    return byWhen_.begin();
}

inline auto
Scheduler::QueueType::end() -> iterator
{
    return byWhen_.end();
}

template <class Handler>
inline auto
Scheduler::QueueType::emplace(time_point when, Handler&& h) -> typename by_when_set::iterator
{
    using event_type = EventImpl<std::decay_t<Handler>>;
    auto const p = alloc_->allocate(sizeof(event_type));
    auto& e = *new (p) event_type(when, std::forward<Handler>(h));
    return byWhen_.insert(e);
}

inline auto
Scheduler::QueueType::erase(iterator iter) -> typename by_when_set::iterator
{
    auto& e = *iter;
    auto next = byWhen_.erase(iter);
    e.~Event();
    alloc_->deallocate(&e, sizeof(e));
    return next;
}

//-----------------------------------------------------------------------------
struct Scheduler::CancelToken
{
private:
    typename QueueType::iterator iter_;

public:
    CancelToken() = delete;
    CancelToken(CancelToken const&) = default;
    CancelToken&
    operator=(CancelToken const&) = default;

private:
    friend class Scheduler;
    CancelToken(typename QueueType::iterator iter) : iter_(iter)
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
Scheduler::at(time_point const& when, Function&& f) -> CancelToken
{
    return queue_.emplace(when, std::forward<Function>(f));
}

template <class Function>
inline auto
Scheduler::in(duration const& delay, Function&& f) -> CancelToken
{
    return at(clock_.now() + delay, std::forward<Function>(f));
}

inline void
Scheduler::cancel(CancelToken const& token)
{
    queue_.erase(token.iter_);
}

inline bool
Scheduler::stepOne()
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
    if (!stepOne())
        return false;
    for (;;)
    {
        if (!stepOne())
            break;
    }
    return true;
}

template <class Function>
inline bool
Scheduler::stepWhile(Function&& f)
{
    bool ran = false;
    while (f() && stepOne())
        ran = true;
    return ran;
}

inline bool
Scheduler::stepUntil(time_point const& until)
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
        stepOne();
        iter = queue_.begin();
    } while (iter != queue_.end() && iter->when <= until);
    clock_.set(until);
    return iter != queue_.end();
}

template <class Period, class Rep>
inline bool
Scheduler::stepFor(std::chrono::duration<Period, Rep> const& amount)
{
    return stepUntil(now() + amount);
}

}  // namespace xrpl::test::csf
