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

// Implementation notes
//
// - A Page is a large allocation from a global PageAllocator.
//
// - Each thread maintains an 'active' page from which it makes allocations.
//
// - When the active page is full, a new one takes it's place.
//
// - Page memory is deallocated when it is not active and no longer referenced.
//
// - Each instance of FifoFreeStoreWithTLS maintains its own set of per-thread active pages,
//   but uses a global PageAllocator. This reduces memory consumption without
//   affecting performance.
//

#if BEAST_USE_BOOST_FEATURES

// This precedes every allocation
//
struct FifoFreeStoreWithTLS::Header
{
    FifoFreeStoreWithTLS::Page* page;
};

//------------------------------------------------------------------------------

class FifoFreeStoreWithTLS::Page : LeakChecked <Page>, public Uncopyable
{
public:
    explicit Page (const size_t bytes) : m_refs (1)
    {
        m_end = reinterpret_cast <char*> (this) + bytes;
        m_free = reinterpret_cast <char*> (
                     Memory::pointerAdjustedForAlignment (this + 1));
    }

    ~Page ()
    {
        bassert (! m_refs.isSignaled ());
    }

    inline bool release ()
    {
        bassert (! m_refs.isSignaled ());

        return m_refs.release ();
    }

    void* allocate (size_t bytes)
    {
        bassert (bytes > 0);

        char* p = Memory::pointerAdjustedForAlignment (m_free);
        char* free = p + bytes;

        if (free <= m_end)
        {
            m_free = free;

            m_refs.addref ();
        }
        else
        {
            p = 0;
        }

        return p;
    }

private:
    AtomicCounter m_refs; // reference count
    char* m_free;           // next free byte
    char* m_end;            // last free byte + 1
};

//------------------------------------------------------------------------------

class FifoFreeStoreWithTLS::PerThreadData : LeakChecked <PerThreadData>, public Uncopyable
{
public:
    explicit PerThreadData (FifoFreeStoreWithTLS* allocator)
        : m_allocator (*allocator)
        , m_active (m_allocator.newPage ())
    {
    }

    ~PerThreadData ()
    {
        if (m_active->release ())
            m_allocator.deletePage (m_active);
    }

    inline void* allocate (const size_t bytes)
    {
        const size_t headerBytes = Memory::sizeAdjustedForAlignment (sizeof (Header));
        const size_t bytesNeeded = headerBytes + bytes;

        if (bytesNeeded > m_allocator.m_pages->getPageBytes ())
            fatal_error ("the memory request was too large");

        Header* header;

        header = reinterpret_cast <Header*> (m_active->allocate (bytesNeeded));

        if (!header)
        {
            if (m_active->release ())
                deletePage (m_active);

            m_active = m_allocator.newPage ();

            header = reinterpret_cast <Header*> (m_active->allocate (bytesNeeded));
        }

        header->page = m_active;

        return reinterpret_cast <char*> (header) + headerBytes;
    }

private:
    FifoFreeStoreWithTLS& m_allocator;
    Page* m_active;
};

//------------------------------------------------------------------------------

inline FifoFreeStoreWithTLS::Page* FifoFreeStoreWithTLS::newPage ()
{
    return new (m_pages->allocate ()) Page (m_pages->getPageBytes ());
}

inline void FifoFreeStoreWithTLS::deletePage (Page* page)
{
    // Safe, because each thread maintains its own active page.
    page->~Page ();
    PagedFreeStoreType::deallocate (page);
}

FifoFreeStoreWithTLS::FifoFreeStoreWithTLS ()
    : m_pages (PagedFreeStoreType::getInstance ())
{
    //bassert (m_pages->getPageBytes () >= sizeof (Page) + Memory::allocAlignBytes);
}

FifoFreeStoreWithTLS::~FifoFreeStoreWithTLS ()
{
    // Clean up this thread's data before we release
    // the reference to the global page allocator.
    m_tsp.reset (0);
}

//------------------------------------------------------------------------------

void* FifoFreeStoreWithTLS::allocate (const size_t bytes)
{
    PerThreadData* data = m_tsp.get ();

    if (!data)
    {
        data = new PerThreadData (this);
        m_tsp.reset (data);
    }

    return data->allocate (bytes);
}

//------------------------------------------------------------------------------

void FifoFreeStoreWithTLS::deallocate (void* p)
{
    const size_t headerBytes = Memory::sizeAdjustedForAlignment (sizeof (Header));
    Header* const header = reinterpret_cast <Header*> (reinterpret_cast <char*> (p) - headerBytes);
    Page* const page = header->page;

    if (page->release ())
        deletePage (page);
}

#endif
