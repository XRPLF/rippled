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

#ifndef BEAST_FIFOFREESTOREWITHTLS_H_INCLUDED
#define BEAST_FIFOFREESTOREWITHTLS_H_INCLUDED

#if BEAST_USE_BOOST_FEATURES

/*============================================================================*/
/**
  Lock-free and mostly wait-free FIFO memory allocator.

  This allocator is suitable for use with CallQueue and Listeners. It is
  expected that over time, deallocations will occur in roughly the same order
  as allocations.

  @note This implementation uses Thread Local Storage to further improve
        performance. However, it requires boost style thread_specific_ptr.

  @invariant allocate() and deallocate() are fully concurrent.

  @invariant The ABA problem is handled automatically.

  @ingroup beast_concurrent
*/
class BEAST_API FifoFreeStoreWithTLS : public LeakChecked <FifoFreeStoreWithTLS>
{
public:
    FifoFreeStoreWithTLS ();
    ~FifoFreeStoreWithTLS ();

    void* allocate (const size_t bytes);
    static void deallocate (void* const p);

private:
    typedef GlobalPagedFreeStore PagedFreeStoreType;
    struct Header;

    class Page;

    inline Page* newPage ();
    static inline void deletePage (Page* page);

private:
    class PerThreadData;
    boost::thread_specific_ptr <PerThreadData> m_tsp;

    PagedFreeStoreType::Ptr m_pages;
};

#endif

#endif
