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

#ifndef RIPPLE_CORE_WORKERS_H_INCLUDED
#define RIPPLE_CORE_WORKERS_H_INCLUDED

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <string>

namespace ripple {

/** A simple thread pool.

    This class is a simple, fixed-sized thread pool. The pool only tracks
    the number of outstanding tasks, and dispatches work. When the pool detects
    that there is no work to be done, it puts threads to sleep.

    The pool does not decide which task to run; that is handled by a callback
    that the pool invokes.  This makes it possible to implement the dispatch
    strategy (e.g. FIFO or priority queues) that makes sense without requiring
    changes in the thread pool itself.

    Threads will continue to run (or sleep) until the thread pool is stopped
    which can happen via an explicit call to the stop method, or automatically
    by the pool's destructor.

    Note that servicing existing tasks take priority over stopping and stopping
    takes priority over servicing new tasks; this means that once a stop request
    has been made, the worker threads will complete their current tasks (if any)
    and then exit, potentially leaving work unfinished.
 */
class Workers
{
public:
    /** Called to perform tasks as needed. */
    struct Callback
    {
        virtual ~Callback() = default;
        Callback() = default;
        Callback(Callback const&) = delete;
        Callback&
        operator=(Callback const&) = delete;

        /** Select and perform a task.

            The function is invoked precisely once for every call to the
            thread pool's addTask method. It executes on one of the thread
            pool's threads.

            This function should process precisely one task.

            @param instance The worker thread instance.

            @throws This function should NOT throw an exception; if it does
                    the exception will be captured and passed to the
                    uncaughtException callback.

            @see Workers::addTask
        */
        virtual void
        processTask(unsigned int instance) = 0;

        /** Indicates that processTask threw an unexpected exception.

            @param instance The worker thread instance.
            @param eptr The exception that was thrown.
         * */
        virtual void
        uncaughtException(unsigned int, std::exception_ptr)
        {
            // Default implementation does nothing
        }
    };

    /** Create a new thread pool with the given number of worker threads.

        @param callback The task selection & execution algorithm.
        @param name The name for this pool (used to name threads)
        @param size The number of threads (must not be 0!) for this thread pool
    */
    explicit Workers(
        Callback& callback,
        std::string const& name,
        unsigned int count);

    ~Workers();

    /** Retrieve the number of threads in the thread pool. */
    unsigned int
    count() const noexcept
    {
        return threads_.load();
    }

    /** Pause all threads and wait until they are paused.

        If a thread is processing a task it will pause as soon as the task
        completes. There may still be tasks signaled even after all threads
        have paused.

        @note This function is not thread-safe.
    */
    void
    stop();

    /** Add a task to be performed.

        Every call to addTask will eventually result in a call to
        Callback::processTask unless the Workers object is destroyed or
        the number of threads is never set above zero.

        @note This function is thread-safe.
    */
    void
    addTask();

private:
    Callback& callback_;

    /// This represents the total number of threads in the thread pool:
    std::atomic<unsigned int> threads_ = 0;

    /** Queued task tracking

        To minimize overhead, we track two things: total number of tasks
        added, and total number of tasks dispatched. The key insight is
        that both of these numbers are only ever incremented and never
        decremented. Along with atomic operations, this allows us to
        write lock free code.

        The choice of 64-bit unsigned integers helps to avoid overflow. A
        32-bit value would overflow too fast even at low queue rates (e.g.
        at a 1,000 tasks per second, the counter would overflow in about 50
        days). With 64 bits, even at a truly obscene rate of 1,000,000,000
        increments per second, this counter is good for over 580 years, at
        which point the server is really due for a reboot.
     */
    /** @{ */
    /// The total number of tasks that have been queued since startup.
    std::atomic<std::uint64_t> head_ = 1;

    /// The total number of tasks that have been dispatched for processing.
    std::atomic<std::uint64_t> tail_ = 1;
    /** @} */
};

}  // namespace ripple

#endif
