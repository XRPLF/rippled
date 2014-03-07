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

#ifndef BEAST_CORE_THREAD_TRACKEDMUTEXTYPE_H_INCLUDED
#define BEAST_CORE_THREAD_TRACKEDMUTEXTYPE_H_INCLUDED

namespace beast
{

/** A template that gives a Mutex diagnostic tracking capabilities. */
template <typename Mutex>
class TrackedMutexType
    : public TrackedMutex
{
public:
    /** The type of ScopedLock to use with this TrackedMutexType object. */
    typedef detail::TrackedScopedLock <TrackedMutexType <Mutex> > ScopedLockType;

    /** The type of ScopedTrylock to use with this TrackedMutexType object. */
    typedef detail::TrackedScopedTryLock <TrackedMutexType <Mutex> > ScopedTryLockType;

    /** The type of ScopedUnlock to use with this TrackedMutexType object. */
    typedef detail::TrackedScopedUnlock <TrackedMutexType <Mutex> > ScopedUnlockType;

    /** Construct a mutex, keyed to a particular class.
        Just pass 'this' for owner and give it the name of the data member
        of your class.
    */
    template <typename Object>
    TrackedMutexType (Object const* object,
                  String name,
                  char const* fileName,
                  int lineNumber)
        : TrackedMutex (detail::TrackedMutexBasics::createName <Object> (
            name, fileName, lineNumber))
    {
    }

    /** Construct a mutex, without a class association.
        These will all get numbered together as a group.
    */
    TrackedMutexType (String name, char const* fileName, int lineNumber)
        : TrackedMutex (detail::TrackedMutexBasics::createName (name,
            fileName, lineNumber))
    {
    }

    ~TrackedMutexType () noexcept
    {
    }

    inline void lock (char const* fileName, int lineNumber) const noexcept
    {
        block (fileName, lineNumber);
        MutexTraits <Mutex>::lock (m_mutex);
        acquired (fileName, lineNumber);
    }

    inline void unlock () const noexcept
    {
        release ();
        MutexTraits <Mutex>::unlock (m_mutex);
    }

    // VFALCO NOTE: We could use enable_if here...
    inline bool try_lock (char const* fileName, int lineNumber) const noexcept
    {
        bool const success = MutexTraits <Mutex>::try_lock (m_mutex);
        if (success)
        {
            // Hack, call block to prevent counts from going wrong.
            block (fileName, lineNumber);
            acquired (fileName, lineNumber);
        }
        return success;
    }

private:
    Mutex const m_mutex;
};

}  // namespace beast

#endif
