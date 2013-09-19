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

#ifndef BEAST_GLOBALFIFOFREESTORE_H_INCLUDED
#define BEAST_GLOBALFIFOFREESTORE_H_INCLUDED

/*============================================================================*/
/**
  A @ref FifoFreeStoreType singleton.

  @ingroup beast_concurrent
*/
template <class Tag>
class GlobalFifoFreeStore
{
public:
    inline void* allocate (size_t bytes)
    {
        return m_allocator.allocate (bytes);
    }

    static inline void deallocate (void* const p)
    {
        FifoFreeStoreType::deallocate (p);
    }

    typedef SharedPtr <SharedSingleton <GlobalFifoFreeStore> > Ptr;

    static Ptr getInstance ()
    {
        return SharedSingleton <GlobalFifoFreeStore>::getInstance();
    }

public:
    GlobalFifoFreeStore ()
    {
    }

    ~GlobalFifoFreeStore ()
    {
    }

private:
    FifoFreeStoreType m_allocator;
};

#endif
