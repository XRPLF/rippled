//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2019 Ripple Labs Inc.

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

#include <ripple/nodestore/impl/TaskQueue.h>

#include <cassert>

namespace ripple {
namespace NodeStore {

TaskQueue::TaskQueue() : workers_(*this, nullptr, "Shard store taskQueue", 1)
{
}

void
TaskQueue::stop()
{
    workers_.stop();
}

void
TaskQueue::addTask(std::function<void()> task)
{
    {
        std::lock_guard lock{mutex_};
        tasks_.emplace(std::move(task));
    }
    workers_.addTask();
}

void
TaskQueue::processTask(int instance)
{
    std::function<void()> task;

    {
        std::lock_guard lock{mutex_};
        assert(!tasks_.empty());

        task = std::move(tasks_.front());
        tasks_.pop();
    }

    task();
}

}  // namespace NodeStore
}  // namespace ripple
