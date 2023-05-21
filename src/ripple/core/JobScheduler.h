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

#ifndef RIPPLE_CORE_JOBSCHEDULER_H_INCLUDED
#define RIPPLE_CORE_JOBSCHEDULER_H_INCLUDED

#include <ripple/basics/promises.h>
#include <ripple/core/Job.h>
#include <ripple/core/JobQueue.h>

#include <string>

namespace ripple {

struct JobScheduler : public Scheduler
{
    JobQueue& jobQueue_;
    std::string name_;
    JobType jobType_;

    JobScheduler(JobQueue& jobQueue, JobType jobType, std::string&& name)
        : jobQueue_(jobQueue), name_(std::move(name)), jobType_(jobType)
    {
    }

    void
    schedule(job_type&& job) override
    {
        jobQueue_.addJob(jobType_, name_, std::move(job));
    }
};

}  // namespace ripple

#endif
