//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CORE_THREAD_IMPL_UNTRACKEDMUTEX_H_INCLUDED
#define BEAST_CORE_THREAD_IMPL_UNTRACKEDMUTEX_H_INCLUDED

namespace beast
{

/** A drop-in replacement for TrackedMutex without the tracking.
*/
template <typename Mutex>
class UntrackedMutexType
    : public Uncopyable
{
public:
    typedef detail::UntrackedScopedLock <UntrackedMutexType <Mutex> > ScopedLockType;
    typedef detail::UntrackedScopedTryLock <UntrackedMutexType <Mutex> > ScopedTryLockType;
    typedef detail::UntrackedScopedUnlock <UntrackedMutexType <Mutex> > ScopedUnlockType;

    template <typename Object>
    inline UntrackedMutexType (Object const*, String name, char const*, int) noexcept
    {
    }

    inline UntrackedMutexType (String, char const*, int) noexcept
    {
    }

    inline UntrackedMutexType () noexcept
    {
    }

    inline ~UntrackedMutexType () noexcept
    {
    }

    inline void lock () const noexcept
    {
        MutexTraits <Mutex>::lock (m_mutex);
    }

    inline void lock (char const*, int) const noexcept
    {
        MutexTraits <Mutex>::lock (m_mutex);
    }

    inline void unlock () const noexcept
    {
        MutexTraits <Mutex>::unlock (m_mutex);
    }

    // VFALCO NOTE: We could use enable_if here...

    inline bool try_lock () const noexcept
    {
        return MutexTraits <Mutex>::try_lock (m_mutex);
    }

    inline bool try_lock (char const*, int) const noexcept
    {
        return MutexTraits <Mutex>::try_lock (m_mutex);
    }

private:
    Mutex mutable m_mutex;
};

}  // namespace beast

#endif
