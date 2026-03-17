/**
 *
 * TODO: Remove xrpl::basic_semaphore (and this file) and use
 * std::counting_semaphore.
 *
 * Background:
 * - PR: https://github.com/XRPLF/rippled/pull/5512/files
 * - std::counting_semaphore had a bug fixed in both GCC and Clang:
 *     * GCC PR 104928: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104928
 *     * LLVM PR 79265: https://github.com/llvm/llvm-project/pull/79265
 *
 * GCC:
 * According to GCC Bugzilla PR104928
 * (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104928#c15), the fix is
 * scheduled for inclusion in GCC 16.0 (see comment #15, Target
 * Milestone: 16.0). It is not included in GCC 14.x or earlier, and there is no
 * indication that it will be backported to GCC 13.x or 14.x branches.
 *
 * Clang:
 * The fix for is included in Clang 19.1.0+
 *
 * Once the minimum compiler version is updated to > GCC 16.0 or Clang 19.1.0,
 * we can remove this file.
 *
 * WARNING: Avoid using std::counting_semaphore until the minimum compiler
 * version is updated.
 */

#pragma once

#include <condition_variable>
#include <mutex>

namespace xrpl {

template <class Mutex, class CondVar>
class basic_semaphore
{
private:
    Mutex mutex_;
    CondVar cond_;
    std::size_t count_;

public:
    using size_type = std::size_t;

    /** Create the semaphore, with an optional initial count.
        If unspecified, the initial count is zero.
    */
    explicit basic_semaphore(size_type count = 0) : count_(count)
    {
    }

    /** Increment the count and unblock one waiting thread. */
    void
    notify()
    {
        std::lock_guard lock{mutex_};
        ++count_;
        cond_.notify_one();
    }

    /** Block until notify is called. */
    void
    wait()
    {
        std::unique_lock lock{mutex_};
        while (count_ == 0)
            cond_.wait(lock);
        --count_;
    }

    /** Perform a non-blocking wait.
        @return `true` If the wait would be satisfied.
    */
    bool
    try_wait()
    {
        std::lock_guard lock{mutex_};
        if (count_ == 0)
            return false;
        --count_;
        return true;
    }
};

using semaphore = basic_semaphore<std::mutex, std::condition_variable>;

}  // namespace xrpl
