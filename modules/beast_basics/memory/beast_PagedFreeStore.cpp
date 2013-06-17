/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

#define LOG_GC 0

namespace
{

// This is the upper limit on the amount of physical memory an instance of the
// allocator will allow. Going over this limit means that consumers cannot keep
// up with producers, and application logic should be re-examined.
//
// TODO: ENFORCE THIS GLOBALLY? MEASURE IN KILOBYTES AND FORCE KILOBYTE PAGE SIZES
#define HARD_LIMIT 1
const size_t hardLimitMegaBytes = 256;

}

/*

Implementation notes

- There are two pools, the 'hot' pool and the 'cold' pool.

- When a new page is needed we pop from the 'fresh' stack of the hot pool.

- When a page is deallocated it is pushed to the 'garbage' stack of the hot pool.

- Every so often, a garbage collection is performed on a separate thread.
  During collection, fresh and garbage are swapped in the cold pool.
  Then, the hot and cold pools are atomically swapped.

*/
//------------------------------------------------------------------------------

struct PagedFreeStore::Page : Pages::Node, LeakChecked <Page>
{
    explicit Page (PagedFreeStore* const allocator)
        : m_allocator (*allocator)
    {
    }

    PagedFreeStore& getAllocator () const
    {
        return m_allocator;
    }

private:
    PagedFreeStore& m_allocator;
};

inline void* PagedFreeStore::fromPage (Page* const p)
{
    return reinterpret_cast <char*> (p) +
           Memory::sizeAdjustedForAlignment (sizeof (Page));
}

inline PagedFreeStore::Page* PagedFreeStore::toPage (void* const p)
{
    return reinterpret_cast <Page*> (
               (reinterpret_cast <char*> (p) -
                Memory::sizeAdjustedForAlignment (sizeof (Page))));
}

//------------------------------------------------------------------------------

PagedFreeStore::PagedFreeStore (const size_t pageBytes)
    : m_pageBytes (pageBytes)
    , m_pageBytesAvailable (pageBytes - Memory::sizeAdjustedForAlignment (sizeof (Page)))
    , m_newPagesLeft (int ((hardLimitMegaBytes * 1024 * 1024) / m_pageBytes))
#if LOG_GC
    , m_swaps (0)
#endif
{
    m_hot  = m_pool1;
    m_cold = m_pool2;

    startOncePerSecond ();
}

PagedFreeStore::~PagedFreeStore ()
{
    endOncePerSecond ();

#if LOG_GC
    bassert (!m_used.isSignaled ());
#endif

    dispose (m_pool1);
    dispose (m_pool2);

#if LOG_GC
    bassert (!m_total.isSignaled ());
#endif
}

//------------------------------------------------------------------------------

void* PagedFreeStore::allocate ()
{
    Page* page = m_hot->fresh->pop_front ();

    if (!page)
    {
#if HARD_LIMIT
        const bool exhausted = m_newPagesLeft.release ();

        if (exhausted)
            Throw (Error ().fail (__FILE__, __LINE__,
                                  TRANS ("the limit of memory allocations was reached")));

#endif

        void* storage = ::malloc (m_pageBytes);

        if (!storage)
            Throw (Error ().fail (__FILE__, __LINE__,
                                  TRANS ("a memory allocation failed")));

        page = new (storage) Page (this);

#if LOG_GC
        m_total.addref ();
#endif
    }

#if LOG_GC
    m_used.addref ();
#endif

    return fromPage (page);
}

void PagedFreeStore::deallocate (void* const p)
{
    Page* const page = toPage (p);
    PagedFreeStore& allocator = page->getAllocator ();

    allocator.m_hot->garbage->push_front (page);

#if LOG_GC
    allocator.m_used.release ();
#endif
}

//
// Perform garbage collection.
//
void PagedFreeStore::doOncePerSecond ()
{
    // Physically free one page.
    // This will reduce the working set over time after a spike.
    {
        Page* page = m_cold->garbage->pop_front ();

        if (page)
        {
            page->~Page ();
            ::free (page);
            m_newPagesLeft.addref ();
#ifdef LOG_GC
            m_total.release ();
#endif
        }
    }

    m_cold->fresh->swap (m_cold->garbage);

    // Swap atomically with respect to m_hot
    Pool* temp = m_hot;
    m_hot = m_cold; // atomic
    m_cold = temp;

#if LOG_GC
    String s;
    s << "swap " << String (++m_swaps);
    s << " (" << String (m_used.get ()) << "/"
      << String (m_total.get ()) << " of "
      << String (m_newPagesLeft.get ()) << ")";
    Logger::outputDebugString (s);
#endif
}

void PagedFreeStore::dispose (Pages& pages)
{
    for (;;)
    {
        Page* const page = pages.pop_front ();

        if (page)
        {
            page->~Page ();
            ::free (page);

#if LOG_GC
            m_total.release ();
#endif
        }
        else
        {
            break;
        }
    }
}

void PagedFreeStore::dispose (Pool& pool)
{
    dispose (pool.fresh);
    dispose (pool.garbage);
}
