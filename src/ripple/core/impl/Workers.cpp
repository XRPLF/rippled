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
#include <ripple/core/impl/Workers.h>
#include <cassert>
#include <chrono>
#include <thread>

namespace ripple {

Workers::Workers(
    Callback& callback,
    std::string const& name,
    unsigned int count)
    : callback_(callback)
{
    assert(count != 0);

    while (count--)
    {
        std::thread th(
            [this, name](unsigned int instance) {
                auto const n = name + ":" + std::to_string(instance);

                ++threads_;

                while (!stopping_)
                {
                    auto t = tail_.load();
                    auto h = head_.load();

                    assert(h >= t);

                    beast::setCurrentThreadName(n + " [zZz]");

                    // If there's nothing to do, go to sleep
                    while (h == t && !stopping_.load(std::memory_order_relaxed))
                    {
                        paused_++;
                        {
                            std::unique_lock lock(mtx_);
                            cv_.wait_for(lock, std::chrono::seconds(5));
                        }
                        t = tail_.load();
                        h = head_.load();
                        paused_--;
                    }

                    if (!stopping_.load(std::memory_order_relaxed) && h > t)
                    {
                        // As long as we aren't stopping and there is a task
                        // that's waiting, we try to do work.
                        if (tail_.compare_exchange_strong(t, t + 1))
                        {
                            // Restore the name, in case the previous callback
                            // had changed it.
                            beast::setCurrentThreadName(n);

                            // Exceptions should never bubble up to here but
                            // just in case, if one does catch it.
                            try
                            {
                                callback_.processTask(instance);
                            }
                            catch (...)
                            {
                                callback_.uncaughtException(
                                    instance, std::current_exception());
                            }
                        }
                    }
                }

                // Track number of threads
                --threads_;
            },
            count);

        // We detach from this thread; it will continue to run in the
        // background until we instruct it to stop using stopping_ as
        // the signal.
        th.detach();
    };
}

Workers::~Workers()
{
    if (threads_.load() != 0)
        stop();

    assert(stopping_.load() && threads_.load() == 0);
}

void
Workers::stop()
{
    if (threads_.load() != 0)
    {
        if (!stopping_.exchange(true) || (threads_.load() != 0))
            cv_.notify_all();
    }

    while (threads_.load() != 0)
        std::this_thread::yield();
}

void
Workers::addTask()
{
    if (stopping_)
        return;

    head_++;

    if (paused_ != 0)
        cv_.notify_one();
}

}  // namespace ripple
