#pragma once

#include <utility>

namespace xrpl {

/// Coroutine stack size (1.5 MB). Increased from 1 MB because
/// ASAN-instrumented deep call stacks exceeded the original limit.
constexpr std::size_t kCoroStackSize = 1536 * 1024;

template <class F>
JobQueue::Coro::Coro(CoroCreateT, JobQueue& jq, JobType type, std::string name, F&& f)
    : jq_(jq)
    , type_(type)
    , name_(std::move(name))
    , coro_(
          boost::context::protected_fixedsize_stack(kCoroStackSize),
          [this, fn = std::forward<F>(f)](boost::coroutines2::coroutine<void>::push_type& doYield) {
              yield_ = &doYield;
              yield();
              fn(shared_from_this());
#ifndef NDEBUG
              finished_ = true;
#endif
          })
{
}

inline JobQueue::Coro::~Coro()
{
#ifndef NDEBUG
    XRPL_ASSERT(finished_, "xrpl::JobQueue::Coro::~Coro : is finished");
#endif
}

inline void
JobQueue::Coro::yield() const
{
    {
        std::scoped_lock const lock(jq_.mutex_);
        ++jq_.nSuspend_;
    }
    (*yield_)();
}

inline bool
JobQueue::Coro::post()
{
    {
        std::scoped_lock const lk(mutex_run_);
        running_ = true;
    }

    // sp keeps 'this' alive
    if (jq_.addJob(type_, name_, [this, sp = shared_from_this()]() { resume(); }))
    {
        return true;
    }

    // The coroutine will not run.  Clean up running_.
    std::scoped_lock const lk(mutex_run_);
    running_ = false;
    cv_.notify_all();
    return false;
}

inline void
JobQueue::Coro::resume()
{
    {
        std::scoped_lock const lk(mutex_run_);
        running_ = true;
    }
    {
        std::scoped_lock const lk(jq_.mutex_);
        --jq_.nSuspend_;
    }
    auto saved = detail::getLocalValues().release();
    detail::getLocalValues().reset(&lvs_);
    std::scoped_lock const lock(mutex_);
    // A late resume() can arrive after the coroutine has already completed.
    // This is an expected (if rare) outcome of the race condition documented
    // in JobQueue.h:354-377 where post() schedules a resume job before the
    // coroutine yields — the mutex serializes access, but by the time this
    // resume() acquires the lock the coroutine may have already run to
    // completion. Calling operator() on a completed boost::coroutine2 is
    // undefined behavior, so we must check and skip invoking the coroutine
    // body if it has already completed.
    if (coro_)
    {
        coro_();
    }
    detail::getLocalValues().release();
    detail::getLocalValues().reset(saved);
    std::scoped_lock const lk(mutex_run_);
    running_ = false;
    cv_.notify_all();
}

inline bool
JobQueue::Coro::runnable() const
{
    return static_cast<bool>(coro_);
}

inline void
JobQueue::Coro::expectEarlyExit()
{
#ifndef NDEBUG
    if (!finished_)
#endif
    {
        // expectEarlyExit() must only ever be called from outside the
        // Coro's stack.  It you're inside the stack you can simply return
        // and be done.
        //
        // That said, since we're outside the Coro's stack, we need to
        // decrement the nSuspend that the Coro's call to yield caused.
        std::scoped_lock const lock(jq_.mutex_);
        --jq_.nSuspend_;
#ifndef NDEBUG
        finished_ = true;
#endif
    }
}

inline void
JobQueue::Coro::join()
{
    std::unique_lock<std::mutex> lk(mutex_run_);
    cv_.wait(lk, [this]() { return !running_; });
}

}  // namespace xrpl
