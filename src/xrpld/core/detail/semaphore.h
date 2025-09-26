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

/**
 *
 * TODO: Remove ripple::basic_semaphore (and this file) and use
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

#ifndef XRPL_CORE_SEMAPHORE_H_INCLUDED
#define XRPL_CORE_SEMAPHORE_H_INCLUDED

#include <condition_variable>
#include <mutex>

namespace ripple {

template <class Mutex, class CondVar>
class basic_semaphore
{
private:
    Mutex m_mutex;
    CondVar m_cond;
    std::size_t m_count;

public:
    using size_type = std::size_t;

    /** Create the semaphore, with an optional initial count.
        If unspecified, the initial count is zero.
    */
    explicit basic_semaphore(size_type count = 0) : m_count(count)
    {
    }

    /** Increment the count and unblock one waiting thread. */
    void
    notify()
    {
        std::lock_guard lock{m_mutex};
        ++m_count;
        m_cond.notify_one();
    }

    /** Block until notify is called. */
    void
    wait()
    {
        std::unique_lock lock{m_mutex};
        while (m_count == 0)
            m_cond.wait(lock);
        --m_count;
    }

    /** Perform a non-blocking wait.
        @return `true` If the wait would be satisfied.
    */
    bool
    try_wait()
    {
        std::lock_guard lock{m_mutex};
        if (m_count == 0)
            return false;
        --m_count;
        return true;
    }
};

using semaphore = basic_semaphore<std::mutex, std::condition_variable>;

}  // namespace ripple

#endif
