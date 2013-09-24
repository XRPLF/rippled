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

#ifndef BEAST_FIFOFREESTOREWITHOUTTLS_H_INCLUDED
#define BEAST_FIFOFREESTOREWITHOUTTLS_H_INCLUDED

/*============================================================================*/
/**
  Lock-free FIFO memory allocator.

  This allocator is suitable for use with CallQueue and Listeners. It is
  expected that over time, deallocations will occur in roughly the same order
  as allocations.

  @note This version of the fifo free store uses less memory and doesn't require
        thread specific storage. However, it runs slower. The performance
  differences are negligible for desktop class applications.

  @invariant allocate() and deallocate() are fully concurrent.

  @invariant The ABA problem is handled automatically.

  @ingroup beast_concurrent
*/
class BEAST_API FifoFreeStoreWithoutTLS : LeakChecked <FifoFreeStoreWithoutTLS>
{
public:
    explicit FifoFreeStoreWithoutTLS ();
    ~FifoFreeStoreWithoutTLS ();

    void* allocate (const size_t bytes);
    static void deallocate (void* const p);

private:
    typedef GlobalPagedFreeStore PagedFreeStoreType;

    struct Header;

    class Block;

    inline Block* newBlock ();
    static inline void deleteBlock (Block* b);

private:
    Block* volatile m_active;
    PagedFreeStoreType::Ptr m_pages;
};

#endif
