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

#ifndef RIPPLE_CORE_JOBCORO_H_INCLUDED
#define RIPPLE_CORE_JOBCORO_H_INCLUDED

#include <ripple/core/Job.h>
#include <beast/win32_workaround.h>
#include <boost/coroutine/all.hpp>
#include <boost/optional.hpp>
#include <boost/thread/tss.hpp>
#include <string>
#include <memory>
#include <mutex>

namespace ripple {

class JobQueue;

namespace detail {

struct JobCoro_create_t { };

} // detail

class JobCoro : public std::enable_shared_from_this<JobCoro>
{
private:
    struct Context
    {
        struct BasicValue
        {
            virtual ~BasicValue() = default;
            virtual void* get() = 0;
        };

        template <class T>
        struct Value : BasicValue
        {
            boost::optional<T> t;

            void* get() override
            {
                return &t;
            }
        };

        std::unordered_map<void const*,
            std::unique_ptr<BasicValue>> values;

        static
        inline
        void
        cleanup (Context*)
        {
        }
    };

    static
    boost::thread_specific_ptr<Context>&
    context_sp()
    {
        static boost::thread_specific_ptr<
            Context> tsp(&Context::cleanup);
        return tsp;
    }

    Context ctx_;
    JobQueue& jq_;
    JobType type_;
    std::string name_;
    std::mutex mutex_;
    boost::coroutines::asymmetric_coroutine<void>::pull_type coro_;
    boost::coroutines::asymmetric_coroutine<void>::push_type* yield_;

public:
    template <class T>
    class LocalValue
    {
    public:
        boost::optional<T>&
        get() const
        {
            auto const ctx = context_sp().get();
            if (! ctx)
                throw std::runtime_error("no coroutine");
            {
                auto const iter = ctx->values.find(this);
                if (iter != ctx->values.end())
                    return *reinterpret_cast<boost::optional<T>*>(
                        result.first->second->get());
            }
            auto const result = ctx->values.emplace(this,
                std::make_unique<Context::Value<T>>());
            return result.first->second;
        }
    };

    // Private: Used in the implementation
    template <class F>
    JobCoro (detail::JobCoro_create_t, JobQueue&, JobType,
        std::string const&, F&&);

    /** Suspend coroutine execution.
        Effects:
          The coroutine's stack is saved.
          The associated Job thread is released.
        Note:
          The associated Job function returns.
          Undefined behavior if called consecutively without a corresponding post.
    */
    void yield() const;

    /** Schedule coroutine execution
        Effects:
          Returns immediately.
          A new job is scheduled to resume the execution of the coroutine.
          When the job runs, the coroutine's stack is restored and execution
            continues at the beginning of coroutine function or the statement
            after the previous call to yield.
        Undefined behavior if called consecutively without a corresponding yield.
    */
    void post();

    /** Return `true` if a coroutine is running on this thread. */
    static
    bool
    onCoro()
    {
        return context_sp().get() != nullptr;
    }
};

} // ripple

#endif
