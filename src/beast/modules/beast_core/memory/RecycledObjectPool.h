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

#ifndef BEAST_RECYCLEDOBJECTPOOL_H_INCLUDED
#define BEAST_RECYCLEDOBJECTPOOL_H_INCLUDED

/** A pool of objects which may be recycled.

    This is a thread safe pool of objects that get re-used. It is
    primarily designed to eliminate the need for many memory allocations
    and frees when temporary buffers are needed for operations.

    To use it, first declare a structure containing the information
    that you want to recycle. Then when you want to use a recycled object
    put a ScopedItem on your stack:

    @code

    struct StdString
    {
        std::string data;
    };

    RecycledObjectPool <StdString> pool;

    void foo ()
    {
        RecycledObjectPool <StdString>::ScopedItem item;

        item.getObject ().data = "text";
    }

    @endcode
*/
template <class Object>
class RecycledObjectPool
{
public:
    struct Item : Object, LockFreeStack <Item>::Node, LeakChecked <Item>
    {
    };

    class ScopedItem
    {
    public:
        explicit ScopedItem (RecycledObjectPool <Object>& pool)
            : m_pool (pool)
            , m_item (pool.get ())
        {
        }

        ~ScopedItem ()
        {
            m_pool.release (m_item);
        }

        Object& getObject () noexcept
        {
            return *m_item;
        }

    private:
        RecycledObjectPool <Object>& m_pool;
        Item* const m_item;
    };

public:
    RecycledObjectPool () noexcept
    {
    }

    ~RecycledObjectPool ()
    {
        for (;;)
        {
            Item* const item = m_stack.pop_front ();

            if (item != nullptr)
                delete item;
            else
                break;
        }
    }

private:
    Item* get ()
    {
        Item* item = m_stack.pop_front ();

        if (item == nullptr)
        {
            item = new Item;

            if (item == nullptr)
                Throw (std::bad_alloc ());
        }

        return item;
    }

    void release (Item* item) noexcept
    {
        m_stack.push_front (item);
    }

private:
    LockFreeStack <Item> m_stack;
};

#endif
