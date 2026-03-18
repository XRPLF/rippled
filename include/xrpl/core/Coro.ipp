#pragma once

#include <xrpl/basics/ByteUtilities.h>

#include <cstddef>

namespace xrpl {

// Sanitizers significantly increase stack frame sizes
// (TSAN ~3-5x, ASAN ~2-3x), requiring larger coroutine stacks.
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
inline constexpr std::size_t coroStackSize = megabytes(4);
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
inline constexpr std::size_t coroStackSize = megabytes(4);
#else
inline constexpr std::size_t coroStackSize = megabytes(1);
#endif
#else
inline constexpr std::size_t coroStackSize = megabytes(1);
#endif

template <class F>
JobQueue::Coro::Coro(Coro_create_t, JobQueue& jq, JobType type, std::string const& name, F&& f)
    : jq_(jq)
    , type_(type)
    , name_(name)
    , running_(false)
    , coro_(
          boost::context::protected_fixedsize_stack(coroStackSize),
          [this, fn = std::forward<F>(f)](
              boost::coroutines2::asymmetric_coroutine<void>::push_type& do_yield) {
              yield_ = &do_yield;
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
        std::lock_guard lock(jq_.m_mutex);
        ++jq_.nSuspend_;
    }
    (*yield_)();
}

inline bool
JobQueue::Coro::post()
{
    {
        std::lock_guard lk(mutex_run_);
        running_ = true;
    }

    // sp keeps 'this' alive
    if (jq_.addJob(type_, name_, [this, sp = shared_from_this()]() { resume(); }))
    {
        return true;
    }

    // The coroutine will not run.  Clean up running_.
    std::lock_guard lk(mutex_run_);
    running_ = false;
    cv_.notify_all();
    return false;
}

inline void
JobQueue::Coro::resume()
{
    {
        std::lock_guard lk(mutex_run_);
        running_ = true;
    }
    {
        std::lock_guard lock(jq_.m_mutex);
        --jq_.nSuspend_;
    }
    auto saved = detail::getLocalValues().release();
    detail::getLocalValues().reset(&lvs_);
    std::lock_guard lock(mutex_);
    XRPL_ASSERT(static_cast<bool>(coro_), "xrpl::JobQueue::Coro::resume : is runnable");
    coro_();
    detail::getLocalValues().release();
    detail::getLocalValues().reset(saved);
    std::lock_guard lk(mutex_run_);
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
        std::lock_guard lock(jq_.m_mutex);
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
    cv_.wait(lk, [this]() { return running_ == false; });
}

}  // namespace xrpl
