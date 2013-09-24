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

#ifndef BEAST_GLOBALPAGEDFREESTORE_H_INCLUDED
#define BEAST_GLOBALPAGEDFREESTORE_H_INCLUDED

/*============================================================================*/
/**
  A PagedFreeStore singleton.

  @ingroup beast_concurrent
*/
class BEAST_API GlobalPagedFreeStore : public LeakChecked <GlobalPagedFreeStore>
{
public:
    GlobalPagedFreeStore ();
    ~GlobalPagedFreeStore ();

public:
    inline size_t getPageBytes ()
    {
        return m_allocator.getPageBytes ();
    }

    inline void* allocate ()
    {
        return m_allocator.allocate ();
    }

    static inline void deallocate (void* const p)
    {
        PagedFreeStore::deallocate (p);
    }

    typedef SharedPtr <SharedSingleton <GlobalPagedFreeStore> > Ptr;
    
    static Ptr getInstance ();

private:
    PagedFreeStore m_allocator;
};

#endif
