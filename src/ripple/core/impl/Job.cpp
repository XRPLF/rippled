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

#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/core/Job.h>
#include <cassert>

namespace ripple {

Job::Job(
    JobType type,
    std::string name,
    std::uint64_t index,
    std::reference_wrapper<LoadSampler const> sample,
    std::function<void()> job)
    : type_(type)
    , index_(index)
    , queued_(clock_type::now())
    , work_(std::move(job))
    , loadEvent_(sample, std::move(name), false)
{
}

Job::Job(Job&& other)
    : type_(other.type_)
    , index_(other.index_)
    , queued_(other.queued_)
    , work_(std::move(other.work_))
    , loadEvent_(std::move(other.loadEvent_))

{
}

JobType
Job::getType() const
{
    return type_;
}

Job::clock_type::time_point const&
Job::queue_time() const
{
    return queued_;
}

std::string const&
Job::description() const
{
    return loadEvent_.name();
}

void
Job::execute()
{
    beast::setCurrentThreadName("Job: " + loadEvent_.name());

    loadEvent_.start();

    work_();

    // Destroy the lambda, otherwise we won't include
    // its duration in the time measurement
    work_ = nullptr;
}

}  // namespace ripple
