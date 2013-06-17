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

#ifndef BEAST_FIFOFREESTOREWITHTLS_BEASTHEADER
#define BEAST_FIFOFREESTOREWITHTLS_BEASTHEADER

#include "beast_GlobalPagedFreeStore.h"

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
class FifoFreeStoreWithTLS
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
