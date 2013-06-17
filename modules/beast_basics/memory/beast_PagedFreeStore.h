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

#ifndef BEAST_PAGEDFREESTORE_BEASTHEADER
#define BEAST_PAGEDFREESTORE_BEASTHEADER

/*============================================================================*/
/**
  Lock-free memory allocator for fixed size pages.

  The ABA problem (http://en.wikipedia.org/wiki/ABA_problem) is avoided by
  treating freed pages as garbage, and performing a collection every second.

  @ingroup beast_concurrent
*/
class PagedFreeStore : private OncePerSecond
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
            Throw (Error ().fail (__FILE__, __LINE__, "the size is too large"));

        return allocate ();
    }

    void* allocate ();
    static void deallocate (void* const p);

private:
    void* newPage ();
    void doOncePerSecond ();

private:
    struct Page;
    typedef LockFreeStack <Page> Pages;

    struct Pool
    {
        CacheLine::Padded <Pages> fresh;
        CacheLine::Padded <Pages> garbage;
    };

    static inline void* fromPage (Page* const p);
    static inline Page* toPage (void* const p);

    void dispose (Pages& pages);
    void dispose (Pool& pool);

private:
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
