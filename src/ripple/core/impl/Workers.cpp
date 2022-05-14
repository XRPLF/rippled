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
#include <thread>

namespace ripple {

Workers::Workers(Callback& callback, std::string name, unsigned int count)
    : m_callback(callback)
{
    assert(count != 0);

    // This loop is constructed so that even if `count` is zero it will
    // always create one thread.
    do
    {
        std::thread t(
            [this, name](unsigned int instance) {
                auto const n = name + ":" + std::to_string(instance);

                beast::setCurrentThreadName(name + " [zZz]");

                while (!stopping_)
                {
                    auto tasks = tasks_.load();

                    // No task to do, so we can go to sleep
                    if (tasks == 0)
                    {
                        paused_++;
                        std::unique_lock lock(m_mut);
                        m_cv.wait(lock, [this]() {
                            return tasks_.load() != 0 || stopping_;
                        });
                        tasks = tasks_.load();
                        paused_--;
                    }

                    if (tasks != 0)
                    {
                        // As long as we aren't stopping and there is a task
                        // that's waiting, we try to do work.
                        if (!stopping_ &&
                            tasks_.compare_exchange_strong(tasks, tasks - 1))
                        {
                            // Restore the name, in case the previous callback
                            // had changed it.
                            beast::setCurrentThreadName(name);

                            // Exceptions should never bubble up to here but
                            // just in case, if one does catch it.
                            try
                            {
                                m_callback.processTask(instance);
                            }
                            catch (...)
                            {
                                m_callback.uncaughtException(
                                    instance, std::current_exception());
                            }
                        }
                    }

                    beast::setCurrentThreadName(name + " [zZz]");
                }

                // Notify the pool that we have exited
                stopped_++;
            },
            threads_++);
        t.detach();
    } while (threads_ < count);
}

Workers::~Workers()
{
    stop();
}

void
Workers::stop()
{
    std::unique_lock lock(m_mut);

    if (!stopped_.exchange(true) || paused_.load())
        m_cv.notify_all();

    m_cv.wait(lock, [this] { return stopped_ == threads_; });
}

void
Workers::addTask()
{
    if (!stopping_)
    {
        // If there were no tasks queued and a thread is available, use it:
        if (tasks_++ == 1 && paused_ != 0)
            m_cv.notify_one();
    }
}

}  // namespace ripple
