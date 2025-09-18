//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_COROINL_H_INCLUDED
#define RIPPLE_CORE_COROINL_H_INCLUDED

#include <xrpl/basics/ByteUtilities.h>

namespace ripple {

template <class F>
JobQueue::Coro::Coro(
    Coro_create_t,
    JobQueue& jq,
    JobType type,
    std::string const& name,
    F&& f)
    : jq_(jq)
    , type_(type)
    , name_(name)
    , coro_(
          [this, fn = std::forward<F>(f)](
              boost::coroutines::asymmetric_coroutine<void>::push_type&
                  do_yield) {
              yield_ = &do_yield;
              yield();
              // self makes Coro alive until this function returns
              std::shared_ptr<Coro> self;
              if (!shouldStop())
              {
                  self = shared_from_this();
                  fn(self);
              }
              {
                  state_ = CoroState::Finished;
                  cv_.notify_all();
              }
          },
          boost::coroutines::attributes(megabytes(1)))
{
}

inline JobQueue::Coro::~Coro()
{
    XRPL_ASSERT(
        state_ != CoroState::Running,
        "ripple::JobQueue::Coro::~Coro : is not running");
    exiting_ = true;
    // Resume the coroutine so that it has a chance to clean things up
    if (state_ == CoroState::Suspended)
    {
        resume();
    }

#ifndef NDEBUG
    XRPL_ASSERT(
        state_ == CoroState::Finished,
        "ripple::JobQueue::Coro::~Coro : is finished");
#endif
}

inline void
JobQueue::Coro::yield()
{
    {
        std::lock_guard lock(jq_.m_mutex);
        if (shouldStop())
            return;

        {
            std::lock_guard stateLock{mutex_run_};
            state_ = CoroState::Suspended;
            cv_.notify_all();
        }
        ++jq_.nSuspend_;
        jq_.m_suspendedCoros[this] = weak_from_this();
        jq_.cv_.notify_all();
    }
    (*yield_)();
}

inline bool
JobQueue::Coro::post()
{
    XRPL_ASSERT(state_ == CoroState::Suspended, "JobQueue::Coro::post: coroutine should be suspended!");

    // sp keeps 'this' alive
    if (jq_.addJob(
            type_, name_, [this, sp = shared_from_this()]() { resume(); }))
    {
        return true;
    }

    cv_.notify_all();
    return false;
}

inline void
JobQueue::Coro::resume()
{
    {
        if (state_ != CoroState::Suspended)
        {
            return;
        }
        state_ = CoroState::Running;
        cv_.notify_all();
    }
    {
        std::lock_guard lock(jq_.m_mutex);
        jq_.m_suspendedCoros.erase(this);
        --jq_.nSuspend_;
        jq_.cv_.notify_all();
    }
    auto saved = detail::getLocalValues().release();
    detail::getLocalValues().reset(&lvs_);
    std::lock_guard lock(mutex_);
    XRPL_ASSERT(
        static_cast<bool>(coro_),
        "ripple::JobQueue::Coro::resume : is runnable");
    coro_();
    detail::getLocalValues().release();
    detail::getLocalValues().reset(saved);
}

inline bool
JobQueue::Coro::runnable() const
{
    // There's an edge case where the coroutine has updated the status
    // to Finished but the function hasn't exited and therefore, coro_ is
    // still valid. However, the coroutine is not technically runnable in this
    // case, because the coroutine is about to exit and static_cast<bool>(coro_)
    // is going to be false.
    return static_cast<bool>(coro_) && state_ != CoroState::Finished;
}

inline void
JobQueue::Coro::join()
{
    std::unique_lock<std::mutex> lk(mutex_run_);
    cv_.wait(lk, [this]() { return state_ != CoroState::Running; });
}

}  // namespace ripple

#endif
