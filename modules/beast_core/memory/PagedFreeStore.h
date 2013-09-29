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

#ifndef BEAST_PAGEDFREESTORE_H_INCLUDED
#define BEAST_PAGEDFREESTORE_H_INCLUDED

/*============================================================================*/
/**
  Lock-free memory allocator for fixed size pages.

  The ABA problem (http://en.wikipedia.org/wiki/ABA_problem) is avoided by
  treating freed pages as garbage, and performing a collection every second.

  @ingroup beast_concurrent
*/
class BEAST_API PagedFreeStore : private DeadlineTimer::Listener
{
public:
    explicit PagedFreeStore (const size_t pageBytes);
    ~PagedFreeStore ();

    // The available bytes per page is a little bit less
    // than requested in the constructor, due to overhead.
    //
    inline size_t getPageBytes () const
    {
        return m_pageBytesAvailable;
    }

    inline void* allocate (const size_t bytes)
    {
        if (bytes > m_pageBytes)
            fatal_error ("the size is too large");

        return allocate ();
    }

    void* allocate ();
    static void deallocate (void* const p);

private:
    void* newPage ();
    void  onDeadlineTimer (DeadlineTimer&);

private:
    struct Page;
    typedef LockFreeStack <Page> Pages;

    struct Pool
    {
        Pool()
            : fresh (&fresh_c.get())
            , garbage (&garbage_c.get())
        {
        }

        Pages* fresh;
        Pages* garbage;
        CacheLine::Padded <Pages> fresh_c;
        CacheLine::Padded <Pages> garbage_c;
    };

    static inline void* fromPage (Page* const p);
    static inline Page* toPage (void* const p);

    void dispose (Pages& pages);
    void dispose (Pool& pool);

private:
    DeadlineTimer m_timer;
    const size_t m_pageBytes;
    const size_t m_pageBytesAvailable;
    CacheLine::Aligned <Pool> m_pool1;  // pair of pools
    CacheLine::Aligned <Pool> m_pool2;
    Pool* volatile m_cold;            // pool which is cooling down
    Pool* volatile m_hot;             // pool we are currently using
    AtomicCounter m_newPagesLeft; // limit of system allocations

#if 1
    int m_swaps;
    AtomicCounter m_total;
    AtomicCounter m_used;
#endif
};

#endif
