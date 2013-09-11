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

#ifndef BEAST_ALLOCATEDBY_H_INCLUDED
#define BEAST_ALLOCATEDBY_H_INCLUDED

/*============================================================================*/
/**
  Customized allocation for heap objects.

  Derived classes will use the specified allocator for new and delete.

  @param AllocatorType The type of allocator to use.

  @ingroup beast_concurrent
*/
template <class AllocatorType>
class AllocatedBy
{
public:
    static inline void* operator new (size_t bytes, AllocatorType& allocator) noexcept
    {
        return allocator.allocate (bytes);
    }

    static inline void* operator new (size_t bytes, AllocatorType* allocator) noexcept
    {
        return allocator->allocate (bytes);
    }

    static inline void operator delete (void* p, AllocatorType&) noexcept
    {
        AllocatorType::deallocate (p);
    }

    static inline void operator delete (void* p, AllocatorType*) noexcept
    {
        AllocatorType::deallocate (p);
    }

    static inline void operator delete (void* p) noexcept
    {
        AllocatorType::deallocate (p);
    }
};

#endif
