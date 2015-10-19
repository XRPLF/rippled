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

#ifndef RIPPLE_CORE_JOBCOROINL_H_INCLUDED
#define RIPPLE_CORE_JOBCOROINL_H_INCLUDED

namespace ripple {

template <class F>
JobCoro::JobCoro (detail::JobCoro_create_t, JobQueue& jq, JobType type,
    std::string const& name, F&& f)
    : jq_(jq)
    , type_(type)
    , name_(name)
    , coro_(
        [this, fn = std::forward<F>(f)]
        (boost::coroutines::asymmetric_coroutine<void>::push_type& do_yield)
        {
            yield_ = &do_yield;
            (*yield_)();
            fn(shared_from_this());
        }, boost::coroutines::attributes (1024 * 1024))
{
}

inline
void
JobCoro::yield () const
{
    (*yield_)();
}

inline
void
JobCoro::post ()
{
    // sp keeps 'this' alive
    jq_.addJob(type_, name_,
        [this, sp = shared_from_this()](Job&)
        {
            std::lock_guard<std::mutex> lock (mutex_);
            coro_();
        });
}

} // ripple

#endif
