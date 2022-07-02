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
    std::function<void()> const& job)
    : mType(type)
    , mJobIndex(index)
    , mJob(job)
    , m_loadEvent(sample, std::move(name), false)
    , m_queue_time(clock_type::now())
{
}

Job::Job(Job&& other)
    : mType(other.mType)
    , mJobIndex(other.mJobIndex)
    , mJob(std::move(other.mJob))
    , m_loadEvent(std::move(other.m_loadEvent))
    , m_queue_time(other.m_queue_time)
{
}

JobType
Job::getType() const
{
    return mType;
}

Job::clock_type::time_point const&
Job::queue_time() const
{
    return m_queue_time;
}

void
Job::doJob()
{
    beast::setCurrentThreadName("Job: " + m_loadEvent.name());

    m_loadEvent.start();

    mJob();

    // Destroy the lambda, otherwise we won't include
    // its duration in the time measurement
    mJob = nullptr;
}

bool
Job::operator>(const Job& j) const
{
    if (mType < j.mType)
        return true;

    if (mType > j.mType)
        return false;

    return mJobIndex > j.mJobIndex;
}

bool
Job::operator>=(const Job& j) const
{
    if (mType < j.mType)
        return true;

    if (mType > j.mType)
        return false;

    return mJobIndex >= j.mJobIndex;
}

bool
Job::operator<(const Job& j) const
{
    if (mType < j.mType)
        return false;

    if (mType > j.mType)
        return true;

    return mJobIndex < j.mJobIndex;
}

bool
Job::operator<=(const Job& j) const
{
    if (mType < j.mType)
        return false;

    if (mType > j.mType)
        return true;

    return mJobIndex <= j.mJobIndex;
}

}  // namespace ripple
