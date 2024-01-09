//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_BASICS_PROMISES_H_INCLUDED
#define RIPPLE_BASICS_PROMISES_H_INCLUDED

#include <atomic>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iterator>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace ripple {

#define TO_STRING0(x) #x
#define TO_STRING(x) TO_STRING0(x)
#define ASSERT_OP(op, x, y) \
    if (x op y) { std::cerr << __FILE__ ":" TO_STRING(__LINE__) ": " #x " == " << x << " " #op " " << y << " == " #y << std::endl; std::abort(); }
#define ASSERT_EQ(x, y) ASSERT_OP(!=, x, y)
#define ASSERT_NE(x, y) ASSERT_OP(==, x, y)

template <std::size_t I, typename... Ts>
struct nth_type : public std::tuple_element<I, std::tuple<Ts...>>
{
};

template <std::size_t I, typename... Ts>
using nth_type_t = typename nth_type<I, Ts...>::type;

namespace detail {

struct construct_callbacks
{
    explicit construct_callbacks() = default;
};
struct construct_value
{
    explicit construct_value() = default;
};
struct construct_error
{
    explicit construct_error() = default;
};

template <typename T>
struct Value
{
    T value_;

    template <typename... Args>
    Value(Args&&... args) : value_(std::forward<Args>(args)...)
    {
    }

    T const&
    get() const
    {
        return value_;
    }

    T&
    get()
    {
        return value_;
    }
};

template <>
struct Value<void>
{
    void
    get() const
    {
    }
};

template <typename T, typename C, typename V>
union Storage
{
    using callback_type = C;
    using value_type = V;
    using error_type = std::exception_ptr;

    std::vector<callback_type> callbacks_;
    std::shared_ptr<T> link_;
    Value<value_type> value_;
    error_type error_;

    Storage() : callbacks_{}
    {
    }

    template <typename... Args>
    Storage(construct_value, Args&&... args)
        : value_(std::forward<Args>(args)...)
    {
    }

    // This form was written before landing on the chosen `error_type`.
    template <typename... Args>
    Storage(construct_error, Args&&... args)
        : error_(std::forward<Args>(args)...)
    {
    }

    ~Storage()
    {
    }

    template <typename... Args>
    void
    construct_error(Args&&... args)
    {
        std::construct_at(&error_, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void
    construct_value(Args&&... args)
    {
        std::construct_at<Value<value_type>>(
            &value_, std::forward<Args>(args)...);
    }

    decltype(auto)
    get_value() const
    {
        return value_.get();
    }

    decltype(auto)
    get_value()
    {
        return value_.get();
    }

    void
    destroy_value()
    {
        std::destroy_at<Value<value_type>>(&value_);
    }
};

template <typename T>
struct Fulfiller;

}  // namespace detail

enum State {
    // IDLE STATES
    // In an idle state, the promise is waiting to transition to a terminal
    // state. It holds callbacks in its storage.

    // The initial idle state.
    PENDING,
    // A thread has indicated that it will settle the promise.
    // The promise may never transition back to the `PENDING` state.
    LOCKED,

    // LOCKED STATE
    // In the locked state, a thread is writing the storage.
    // No other thread may read or write the storage.
    WRITING,

    // TERMINAL STATES
    // In a terminal state, the promise will never change states again.
    // Its storage will never be written again, except by the destructor.

    // The promise has been linked to another.
    LINKED,
    // The promise has been settled with a value.
    FULFILLED,
    // The promise has been settled with an error.
    REJECTED,
    // The promise has been settled with nothing.
    CANCELLED,
};

template <typename V>
class AsyncPromise;

template <typename F, typename... Args>
struct ApplyState;

class Scheduler
{
public:
    using job_type = std::function<void()>;

    virtual void
    schedule(job_type&& job) = 0;

    Scheduler() = default;
    Scheduler(Scheduler const&) = delete;
    Scheduler&
    operator=(Scheduler const&) = delete;
    virtual ~Scheduler() = default;

    template <typename V>
    auto
    pending()
    {
        return std::make_shared<AsyncPromise<V>>(
            *this, detail::construct_callbacks{});
    }

    template <typename V, typename... Args>
    auto
    fulfilled(Args&&... args)
    {
        return std::make_shared<AsyncPromise<V>>(
            *this, detail::construct_value{}, std::forward<Args>(args)...);
    }

    template <typename V, typename E>
    auto
    rejected(E const& error)
    {
        return std::make_shared<AsyncPromise<V>>(
            *this, detail::construct_error{}, std::make_exception_ptr(error));
    }

    template <typename F, typename... Args>
    auto
    apply(F&& function, std::shared_ptr<AsyncPromise<Args>>... args) ->
        typename AsyncPromise<std::invoke_result_t<F, Args...>>::pointer_type
    {
        using R = std::invoke_result_t<F, Args...>;
        auto output = pending<R>();
        auto state = std::make_shared<ApplyState<F, Args...>>(
            output, std::move(function));
        state->addCallbacks(args...);
        return output;
        // All of the inputs now hold callbacks that hold a shared pointer to
        // the shared state.
        // We will now release our shared pointer to the shared state.
        // The last input that destroys its calllback will destroy the shared
        // state.
    }
};

/** Shared state collecting arguments for a function application. */
template <typename F, typename... Args>
struct ApplyState : public std::enable_shared_from_this<ApplyState<F, Args...>>
{
    using R = std::invoke_result_t<F, Args...>;
    using output_type = typename AsyncPromise<R>::pointer_type;

    output_type output_;
    F function_;
    std::tuple<std::shared_ptr<const Args>...> arguments_;
    std::atomic<unsigned int> count_ = 0;
    std::atomic<bool> valid_ = true;

    ApplyState(output_type output, F&& function)
        : output_(std::move(output)), function_(std::move(function))
    {
    }

    template <std::size_t I>
    void
    addCallback()
    {
    }

    template <std::size_t I, typename Arg, typename... Rest>
    void
    addCallback(std::shared_ptr<AsyncPromise<Arg>> arg, Rest&&... rest)
    {
        arg->subscribe([self = this->shared_from_this()](auto const& p) {
            self->template setArgument<I>(p);
        });
        addCallback<I + 1>(std::forward<Rest>(rest)...);
    }

    template <typename... Rest>
    void
    addCallbacks(Rest&&... rest)
    {
        addCallback<0>(std::forward<Rest>(rest)...);
    }

    template <std::size_t... I>
    R invoke(std::index_sequence<I...>)
    {
        return std::invoke(
            std::move(function_), std::move(*std::get<I>(arguments_))...);
    }

    R
    invoke()
    {
        return invoke(std::make_index_sequence<sizeof...(Args)>());
    }

    template <std::size_t I>
    void
    setArgument(std::shared_ptr<void> const& arg)
    {
        using Arg = nth_type_t<I, Args...>;
        auto p = static_cast<AsyncPromise<Arg>*>(arg.get())->follow();
        auto state = p->state_();
        if (state == REJECTED)
        {
            bool valid = true;
            if (valid_.compare_exchange_strong(
                    valid, false, std::memory_order_seq_cst))
            {
                // We are the only writer who invalidated this state.
                output_->reject(p->error());
                output_.reset();
            }
        }
        else
        {
            ASSERT_EQ(state, FULFILLED);
            std::get<I>(arguments_) = p->value_ptr();
        }
        auto count = 1 + count_.fetch_add(1, std::memory_order_seq_cst);
        if (count != sizeof...(Args))
        {
            return;
        }
        // We are the argument writer who wrote the final argument.
        // Every other argument writer has already passed the call to
        // `count_.fetch_add`, and the effect of their write to `valid_`, if
        // any, is visible in this thread because of the acquire-release
        // synchronization on `count_`.
        if (!valid_.load(std::memory_order_seq_cst))
        {
            return;
        }
        output_->scheduler_.schedule([self = this->shared_from_this()]() {
            try
            {
                self->output_->fulfill(self->invoke());
            }
            catch (...)
            {
                self->output_->reject(std::current_exception());
            }
            // Normally, this lambda would hold the last reference to the
            // shared state, and the state would be destroyed as the lambda
            // exits, but in case someone else is holding a pointer to the
            // state, we'll release its hold on the output, because it can and
            // will no longer modify it.
            self->output_.reset();
        });
    }
};

template <typename Derived, typename V>
struct Thenable
{
    decltype(auto)
    value_or(V const& deflt = V()) const
    {
        auto self = static_cast<Derived const*>(this)->follow();
        return (self->state() == FULFILLED) ? self->value_() : deflt;
    }
};

template <typename Derived>
struct Thenable<Derived, void>
{
};

template <typename V>
class AsyncPromise : public std::enable_shared_from_this<AsyncPromise<V>>,
                     public Thenable<AsyncPromise<V>, V>
{
public:
    using pointer_type = std::shared_ptr<AsyncPromise<V>>;
    using callback_type = std::function<void(pointer_type const&)>;
    using storage_type = detail::Storage<AsyncPromise<V>, callback_type, V>;
    using value_type = typename storage_type::value_type;
    using error_type = typename storage_type::error_type;

private:
    Scheduler& scheduler_;
    std::atomic<State> status_ = PENDING;
    storage_type storage_;

    template <typename, typename>
    friend struct Thenable;

public:
    AsyncPromise() = delete;
    AsyncPromise(AsyncPromise const&) = delete;
    AsyncPromise(AsyncPromise&&) = delete;

    AsyncPromise(Scheduler& scheduler, detail::construct_callbacks)
        : scheduler_(scheduler)
    {
    }

    template <typename... Args>
    AsyncPromise(
        Scheduler& scheduler,
        detail::construct_value ctor,
        Args&&... args)
        : scheduler_(scheduler)
        , status_(FULFILLED)
        , storage_(ctor, std::forward<Args>(args)...)
    {
    }

    template <typename... Args>
    AsyncPromise(
        Scheduler& scheduler,
        detail::construct_error ctor,
        Args&&... args)
        : scheduler_(scheduler)
        , status_(REJECTED)
        , storage_(ctor, std::forward<Args>(args)...)
    {
    }

    ~AsyncPromise()
    {
        auto status = state_();
        ASSERT_NE(status, WRITING);
        if (status == PENDING || status == LOCKED)
        {
            std::destroy_at(&storage_.callbacks_);
        }
        else if (status == LINKED)
        {
            std::destroy_at(&storage_.link_);
        }
        else if (status == FULFILLED)
        {
            storage_.destroy_value();
        }
        else
        {
            ASSERT_EQ(status, REJECTED);
            std::destroy_at(&storage_.error_);
        }
    }

    Scheduler&
    scheduler() const
    {
        return scheduler_;
    }

    State
    state() const
    {
        auto self = follow();
        return self->state_();
    }

    bool
    settled() const
    {
        auto status = state();
        return status == FULFILLED || status == REJECTED || status == CANCELLED;
    }

    bool
    fulfilled() const
    {
        return state() == FULFILLED;
    }

    bool
    rejected() const
    {
        return state() == REJECTED;
    }

    bool
    cancelled() const
    {
        return state() == CANCELLED;
    }

    bool
    lock()
    {
        auto self = this;
        State previous = transition_(self, LOCKED);
        return previous == PENDING;
    }

    bool
    link(pointer_type const& rhs)
    {
        return link(rhs.get());
    }

    /**
     * For this explanation, consider only the observable states
     * (i.e. those returned by `transition_`):
     * `PENDING`, `LOCKED`, `FULFILLED`, and `REJECTED`.
     * One of the two halves of a link _must_ be in the `PENDING` state and
     * _must never_ transition to a non-`PENDING` state.
     * The other observable states (`LOCKED`, `FULFILLED`, and `REJECTED`)
     * indicate that a thread will settle or has settled the promise,
     * and it is impossible to link two promises that are both non-`PENDING`.
     */
    bool
    link(AsyncPromise* rhs)
    {
        auto lhs = this;
        // Typically, we expect `rhs` to be (a) fresh with no callbacks and
        // (b) the one that will be settled.
        // Thus, we first try to link `rhs` to `lhs`.
        auto rprev = transition_(rhs, WRITING);
        auto lprev = transition_(lhs, WRITING);
        if (rprev != PENDING)
        {
            // `rhs` is settled or locked.
            // `lhs` must be pending.
            if (lprev != PENDING)
            {
                // This should be unreachable, but we can recover
                // as if this method were never called.
                if (rprev == LOCKED)
                {
                    rhs->status_.store(LOCKED, std::memory_order_seq_cst);
                }
                if (lprev == LOCKED)
                {
                    lhs->status_.store(LOCKED, std::memory_order_seq_cst);
                }
                return false;
            }
            std::swap(lhs, rhs);
            std::swap(lprev, rprev);
        }
        // For the rest of the function,
        // we can assume that `rhs` was `PENDING` and is now `WRITING`.
        // We need to reap callbacks from `rhs`, link it to `lhs`,
        // and change its state to `LINKED`.
        // `lhs` may be `WRITING` and its previous state is in `lprev`.
        // If it was not settled (i.e. if it was pending or locked),
        // then its state is now `WRITING`,
        // and we need to add callbacks from `rhs`
        // and restore its previous state.
        // If it was settled,
        // then its state is unchanged,
        // and we need to schedule callbacks from `rhs`,
        // passing to them `lhs`.

        // Reap the callbacks from `rhs`.
        decltype(storage_.callbacks_) callbacks(
            std::move(rhs->storage_.callbacks_));
        std::destroy_at(&rhs->storage_.callbacks_);
        // Make `rhs` link to `lhs`.
        std::construct_at(&rhs->storage_.link_, lhs->shared_from_this());
        rhs->status_.store(LINKED, std::memory_order_seq_cst);

        if (lprev == PENDING || lprev == LOCKED)
        {
            std::move(
                callbacks.begin(),
                callbacks.end(),
                std::back_inserter(lhs->storage_.callbacks_));
            lhs->status_.store(lprev, std::memory_order_seq_cst);
        }
        else if (lprev != CANCELLED)
        {
            for (auto& cb : callbacks)
            {
                lhs->scheduler_.schedule(
                    [self = lhs->shared_from_this(), cb = std::move(cb)]() {
                        cb(std::move(self));
                    });
            }
        }

        return true;
    }

    void
    subscribe(callback_type&& cb)
    {
        auto self = this;
        State previous = transition_(self, WRITING);
        if (previous != PENDING && previous != LOCKED)
        {
            // The promise is settled. No longer taking subscribers.
            if (previous != CANCELLED)
            {
                self->scheduler_.schedule(
                    [self = self->shared_from_this(), cb = std::move(cb)]() {
                        cb(std::move(self));
                    });
            }
            return;
        }
        self->storage_.callbacks_.push_back(std::move(cb));
        self->status_.store(previous, std::memory_order_seq_cst);
    }

    template <typename F>
    decltype(auto)
    then(F&& f)
    {
        using R = std::invoke_result_t<F, pointer_type const&>;
        using Fulfiller = detail::Fulfiller<R>;
        auto q = scheduler_.pending<typename Fulfiller::value_type>();
        auto cb = [q, f = std::move(f)](pointer_type const& p) mutable {
            try
            {
                Fulfiller{q}.fulfillWith(std::move(f), std::move(p));
            }
            catch (...)
            {
                q->reject(std::current_exception());
            }
        };
        subscribe(cb);
        return q;
    }

    // TODO: Any way to distinguish calls to this method
    // if it were an overload of `then`?
    template <typename F>
    decltype(auto)
    thenv(F&& f)
    {
        return then([f = std::forward<F>(f)](auto const& p) {
            // Callbacks should never be called with a linked promise.
            auto state = p->state_();
            if (state == REJECTED)
            {
                std::rethrow_exception(p->error());
            }
            ASSERT_EQ(state, FULFILLED);
            return f(p->value());
        });
    }

    decltype(auto)
    reify() const
    {
        auto self = follow();
        auto status = self->state_();
        if (status == REJECTED)
        {
            std::rethrow_exception(self->error_());
        }
        if (status != FULFILLED)
        {
            throw std::runtime_error("promise not settled");
        }
        return self->value_();
    }

    decltype(auto)
    value() const
    {
        auto self = follow();
        ASSERT_EQ(self->state_(), FULFILLED);
        return self->value_();
    }

    decltype(auto)
    value()
    {
        auto self = follow();
        ASSERT_EQ(self->state_(), FULFILLED);
        return self->value_();
    }

    decltype(auto)
    value_ptr() const
    {
        auto self = follow();
        return std::shared_ptr<const value_type>(
            self->shared_from_this(), &self->value_());
    }

    decltype(auto)
    value_ptr()
    {
        auto self = follow();
        return std::shared_ptr<value_type>(
            self->shared_from_this(), &self->value_());
    }

    error_type const&
    error() const
    {
        auto self = follow();
        ASSERT_EQ(self->state_(), REJECTED);
        return self->error_();
    }

    error_type&
    error()
    {
        auto self = follow();
        ASSERT_EQ(self->state_(), REJECTED);
        return self->error_();
    }

    std::string
    message() const
    {
        try
        {
            std::rethrow_exception(error());
        }
        catch (std::exception const& error)
        {
            return error.what();
        }
        // C++23: std::unreachable().
        return "unreachable";
    }

    template <typename... Args>
    bool
    fulfill(Args&&... args)
    {
        return settle(
            FULFILLED,
            &storage_type::template construct_value<Args...>,
            std::forward<Args>(args)...);
    }

    template <typename E>
    bool
    reject(E&& error)
    {
        return reject(std::make_exception_ptr(std::move(error)));
    }

    bool
    reject(std::exception_ptr error)
    {
        return settle(
            REJECTED,
            &storage_type::template construct_error<std::exception_ptr>,
            std::move(error));
    }

    bool
    cancel()
    {
        auto self = this;
        State previous = transition_(self, CANCELLED);
        if (previous != PENDING && previous != LOCKED)
        {
            // The promise was already settled. Nothing left to do.
            return false;
        }
        // We are the thread that cancelled,
        // and the promsie was in an idle state.
        std::destroy_at(&self->storage_.callbacks_);
        return true;
    }

private:
    template <typename W>
    friend class AsyncPromise;
    template <typename F, typename... Args>
    friend struct ApplyState;

    AsyncPromise const*
    follow() const
    {
        auto p = this;
        while (p->state_() == LINKED)
        {
            p = p->storage_.link_.get();
        }
        return p;
    }

    AsyncPromise*
    follow()
    {
        auto p = this;
        while (p->state_() == LINKED)
        {
            p = p->storage_.link_.get();
        }
        return p;
    }

    State
    state_() const
    {
        return status_.load(std::memory_order_seq_cst);
    }

    decltype(auto)
    value_() const
    {
        return storage_.get_value();
    }

    decltype(auto)
    value_()
    {
        return storage_.get_value();
    }

    error_type const&
    error_() const
    {
        return storage_.error_;
    }

    error_type&
    error_()
    {
        return storage_.error_;
    }

    /**
     * @return `PENDING` or `LOCKED` if the transition succeeded.
     * Otherwise, whatever state prevented the transition
     * from ever succeeding, one of `FULFILLED`, `REJECTED`, or `CANCELLED`
     * (or `LOCKED` only if `desired == LOCKED`).
     * Will never be `WRITING` or `LINKED`.
     */
    friend State
    transition_(AsyncPromise*& self, State desired)
    {
        State idle = PENDING;
        State expected = idle;
        while (!self->status_.compare_exchange_weak(
            expected, desired, std::memory_order_seq_cst))
        {
            if (expected == WRITING)
            {
                // Another thread is writing. Try again.
                expected = idle;
                continue;
            }
            if (expected == LINKED)
            {
                // Follow the link and try again.
                self = self->storage_.link_.get();
                expected = idle;
                continue;
            }
            if (expected == LOCKED && desired != LOCKED)
            {
                // `LOCKED` is the idle state,
                // and this should be the thread that put it there.
                idle = LOCKED;
                // One more time through the loop.
                continue;
            }
            break;
        }
        return expected;
    }

    template <typename M, typename... Args>
    bool
    settle(State status, M method, Args&&... args)
    {
        auto self = this;
        // We cannot transition directly to `status`
        // because we do not want any threads reading the value or error
        // until it is constructed.
        State previous = transition_(self, WRITING);
        if (previous != PENDING && previous != LOCKED)
        {
            // This should be unreachable unless `previous == CANCELLED`.
            // Only one thread should ever call `settle`,
            // but it does not have to check for cancellation.
            ASSERT_EQ(previous, CANCELLED);
            return false;
        }
        auto callbacks = std::move(self->storage_.callbacks_);
        std::destroy_at(&self->storage_.callbacks_);
        (self->storage_.*method)(std::forward<Args>(args)...);
        self->status_.store(status, std::memory_order_seq_cst);
        for (auto& cb : callbacks)
        {
            self->scheduler_.schedule(
                [self = self->shared_from_this(), cb = std::move(cb)]() {
                    cb(std::move(self));
                });
        }
        return true;
    }
};

template <typename T>
using FuturePtr = std::shared_ptr<AsyncPromise<T>>;

namespace detail {

template <typename T>
struct Fulfiller
{
    using value_type = T;

    FuturePtr<value_type>& output_;

    template <typename F, typename... Args>
    bool
    fulfillWith(F&& f, Args&&... args)
    {
        return output_->fulfill(f(std::forward<Args>(args)...));
    }
};

template <>
struct Fulfiller<void>
{
    using value_type = void;

    FuturePtr<value_type>& output_;

    template <typename F, typename... Args>
    bool
    fulfillWith(F&& f, Args&&... args)
    {
        f(std::forward<Args>(args)...);
        return output_->fulfill();
    }
};

template <typename T>
struct Fulfiller<FuturePtr<T>>
{
    using value_type = T;

    FuturePtr<value_type>& output_;

    template <typename F, typename... Args>
    bool
    fulfillWith(F&& f, Args&&... args)
    {
        return output_->link(f(std::forward<Args>(args)...));
    }
};

}  // namespace detail
}  // namespace ripple

#endif
