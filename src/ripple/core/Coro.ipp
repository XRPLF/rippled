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

namespace ripple {

template <class F>
JobQueue::Coro::
Coro(Coro_create_t, JobQueue& jq, JobType type,
    std::string const& name, F&& f)
    : jq_(jq)
    , type_(type)
    , name_(name)
    , running_(false)
    , coro_(
        [this, fn = std::forward<F>(f)]
        (boost::coroutines::asymmetric_coroutine<void>::push_type& do_yield)
        {
            yield_ = &do_yield;
            yield();
            fn(shared_from_this());
#ifndef NDEBUG
            finished_ = true;
#endif
        }, boost::coroutines::attributes (1024 * 1024))
{
}

inline
JobQueue::Coro::
~Coro()
{
    assert(finished_);
}

inline
void
JobQueue::Coro::
yield() const
{
    {
        std::lock_guard<std::mutex> lock(jq_.m_mutex);
        ++jq_.nSuspend_;
    }
    (*yield_)();
}

inline
void
JobQueue::Coro::
post()
{
    {
        std::lock_guard<std::mutex> lk(mutex_run_);
        running_ = true;
    }

    // sp keeps 'this' alive
    jq_.addJob(type_, name_,
        [this, sp = shared_from_this()](Job&)
        {
            {
                std::lock_guard<std::mutex> lock(jq_.m_mutex);
                --jq_.nSuspend_;
            }
            auto saved = detail::getLocalValues().release();
            detail::getLocalValues().reset(&lvs_);
            std::lock_guard<std::mutex> lock(mutex_);
            coro_();
            detail::getLocalValues().release();
            detail::getLocalValues().reset(saved);
            std::lock_guard<std::mutex> lk(mutex_run_);
            running_ = false;
            cv_.notify_all();
        });
}

inline
void
JobQueue::Coro::
join()
{
    std::unique_lock<std::mutex> lk(mutex_run_);
    cv_.wait(lk,
        [this]()
        {
            return running_ == false;
        });
}

} // ripple

#endif
