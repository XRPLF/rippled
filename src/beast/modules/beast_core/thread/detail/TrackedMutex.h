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

#ifndef BEAST_CORE_THREAD_DETAIL_TRACKEDMUTEX_H_INCLUDED
#define BEAST_CORE_THREAD_DETAIL_TRACKEDMUTEX_H_INCLUDED

namespace beast
{

class TrackedMutex;

namespace detail
{

struct TrackedMutexBasics
{
    struct PerThreadData;

    typedef List <TrackedMutex> ThreadLockList;
    typedef List <PerThreadData> GlobalThreadList;

    // Retrieve an atomic counter unique to class Object
    template <typename Object>
    static Atomic <int>& getCounter () noexcept
    {
        static Atomic <int> counter;
        return counter;
    }

    template <typename Object>
    inline static String createName (String name,
        char const* fileName, int lineNumber)
    {
        return createName (name, fileName, lineNumber,
            ++getCounter <Object> ());
    }

    static String createName (String name,
        char const* fileName, int lineNumber, int instanceNumber = 0);

    struct PerThreadData
        : GlobalThreadList::Node
    {
        PerThreadData ();

        int id;
        int refCount;
        ThreadLockList list;
        CriticalSection mutex;

        TrackedMutex const* blocked;
        String threadName;      // at the time of the block
        String sourceLocation;  // at the time of the block
    };

    // This turns the thread local into a POD which will be
    // initialized to all zeroes. We use the 'id' flag to
    // know to initialize.
    //
    struct PerThreadDataStorage
    {
        BEAST_ALIGN(8)
        unsigned char bytes [sizeof (PerThreadData)];
    };

    static Atomic <int> lastThreadId;

    static ThreadLocalValue <PerThreadDataStorage> threadLocal;

    static PerThreadData& getPerThreadData ();

    struct Lists
    {
        GlobalThreadList allThreads;
    };

    static CriticalSection& getGlobalMutex ();
    static Lists& getLists ();
};

} // namespace detail

} // namespace beast

#endif
