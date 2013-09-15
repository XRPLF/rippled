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

#ifndef BEAST_CORE_CONTAINERS_DYNAMICLIST_H_INCLUDED
#define BEAST_CORE_CONTAINERS_DYNAMICLIST_H_INCLUDED

template <typename, typename>
class DynamicList;

template <class Container, bool IsConst>
class DynamicListIterator
    : public std::iterator <
        std::bidirectional_iterator_tag,
        typename Container::value_type,
        typename Container::difference_type,
        typename mpl::IfCond <IsConst,
            typename Container::const_pointer,
            typename Container::pointer>::type,
        typename mpl::IfCond <IsConst,
            typename Container::const_reference,
            typename Container::reference>::type>
{
private:
    typedef typename mpl::IfCond <IsConst,
        typename List <typename Container::Item>::const_iterator,
        typename List <typename Container::Item>::iterator>::type iterator_type;

    typedef typename mpl::IfCond <IsConst,
        typename Container::const_pointer,
        typename Container::pointer>::type pointer;

    typedef typename mpl::IfCond <IsConst,
        typename Container::const_reference,
        typename Container::reference>::type reference;

public:
    DynamicListIterator ()
    {
    }

    DynamicListIterator (iterator_type iter)
        : m_iter (iter)
    {
    }

    DynamicListIterator (DynamicListIterator const& other)
        : m_iter (other.m_iter)
    {
    }

    template <bool OtherIsConst>
    DynamicListIterator (DynamicListIterator <Container, OtherIsConst> const& other)
        : m_iter (other.m_iter)
    {
    }

    DynamicListIterator& operator= (DynamicListIterator const& other)
    {
        m_iter = other.m_iter;
        return *this;
    }

    template <bool OtherIsConst>
    DynamicListIterator& operator= (DynamicListIterator <
        Container, OtherIsConst> const& other)
    {
        m_iter = other.m_iter;
        return *this;
    }

    template <bool OtherIsConst>
    bool operator== (DynamicListIterator <Container, OtherIsConst> const& other) const
    {
        return m_iter == other.m_iter;
    }

    template <bool OtherIsConst>
    bool operator!= (DynamicListIterator <Container, OtherIsConst> const& other) const
    {
        return ! ((*this) == other);
    }

    reference operator* () const
    {
        return dereference ();
    }

    pointer operator-> () const
    {
        return &dereference ();
    }

    DynamicListIterator& operator++ ()
    {
        increment ();
        return *this;
    }

    DynamicListIterator operator++ (int)
    {
        DynamicListIterator const result (*this);
        increment ();
        return result;
    }

    DynamicListIterator& operator-- ()
    {
        decrement ();
        return *this;
    }

    DynamicListIterator operator-- (int)
    {
        DynamicListIterator const result (*this);
        decrement ();
        return result;
    }

private:
    template <typename, typename>
    friend class DynamicList;

    reference dereference () const
    {
        return *(m_iter->get ());
    }

    void increment ()
    {
        ++m_iter;
    }

    void decrement ()
    {
        --m_iter;
    }

    iterator_type m_iter;
};

//------------------------------------------------------------------------------

/** A list that uses a very small number of dynamic allocations.

    Once an element is allocated, its address does not change. Elements
    can be erased, and they are placed onto a deleted list for re-use.
    Allocations occur in configurable batches.

    Iterators to elements never become invalid, they can be safely
    stored elsewhere, as long as the underlying element is not erased.

    T may support these concepts:
        DefaultConstructible
        MoveConstructible (C++11)

    T must support these concepts:
        Destructible
*/
template <typename T,
          class Allocator = std::allocator <char> >
class DynamicList
{
private:
    typedef PARAMETER_TYPE (T) TParam;

public:
    enum
    {
        defaultBlocksize = 1000
    };

    typedef T              value_type;
    typedef Allocator      allocator_type;
    typedef std::size_t    size_type;
    typedef std::ptrdiff_t difference_type;
    typedef T*             pointer;
    typedef T&             reference;
    typedef T const*       const_pointer;
    typedef T const&       const_reference;
    
    typedef DynamicList <
        T,
        Allocator>         container_type;

    typedef DynamicListIterator <container_type, false> iterator;
    typedef DynamicListIterator <container_type, true>  const_iterator;

public:
    explicit DynamicList (
        size_type blocksize = defaultBlocksize,
        Allocator const& allocator = Allocator ())
        : m_allocator (allocator)
        , m_blocksize (blocksize)
        , m_capacity (0)
    {
    }

    ~DynamicList ()
    {
        clear ();
        shrink_to_fit ();
    }

    allocator_type get_allocator () const noexcept
    {
        return m_allocator;
    }

    //--------------------------------------------------------------------------

    reference front () noexcept
    {
        return *m_items.front ().get ();
    }

    const_reference front () const noexcept
    {
        return *m_items.front ().get ();
    }

    reference back () noexcept
    {
        return *m_items.back ().get ();
    }

    const_reference back () const noexcept
    {
        return *m_items.back ().get ();
    }

    //--------------------------------------------------------------------------

    iterator begin () noexcept
    {
        return iterator (m_items.begin ());
    }

    const_iterator begin () const noexcept
    {
        return const_iterator (m_items.begin ());
    }

    const_iterator cbegin () const noexcept
    {
        return const_iterator (m_items.cbegin ());
    }

    iterator end () noexcept
    {
        return iterator (m_items.end ());
    }

    const_iterator end () const noexcept
    {
        return const_iterator (m_items.end ());
    }

    const_iterator cend () const noexcept
    {
        return const_iterator (m_items.cend ());
    }

    iterator iterator_to (T& value) noexcept
    {
        std::ptrdiff_t const offset (
            (std::ptrdiff_t)(((Item const*)0)->get ()));
        Item& item (*addBytesToPointer (((Item*)&value), -offset));
        return iterator (m_items.iterator_to (item));
    }

    const_iterator const_iterator_to (T const& value) const noexcept
    {
        std::ptrdiff_t const offset (
            (std::ptrdiff_t)(((Item const*)0)->get ()));
        Item const& item (*addBytesToPointer (((Item const*)&value), -offset));
        return const_iterator (m_items.const_iterator_to (item));
    }

    //--------------------------------------------------------------------------

    bool empty () const noexcept
    {
        return m_items.empty ();
    }

    size_type size () const noexcept
    {
        return m_items.size ();
    }

    size_type max_size () const noexcept
    {
        return std::numeric_limits <size_type>::max ();
    }

    void reserve (size_type new_cap) noexcept
    {
        new_cap = m_blocksize * (
            (new_cap + m_blocksize - 1) / m_blocksize);
        if (new_cap > max_size ())
            Throw (std::length_error ("new_cap > max_size"), __FILE__, __LINE__);
        if (new_cap <= m_capacity)
            return;
        size_type const n (new_cap / m_blocksize);
        m_handles.reserve (n);
        for (size_type i = m_handles.size (); i < n; ++i)
            m_handles.push_back (static_cast <Item*> (std::malloc (
                m_blocksize * sizeof (Item))));
        m_capacity = new_cap;
    }

    size_type capacity () const noexcept
    {
        return m_capacity;
    }

    void shrink_to_fit ()
    {
        // Special case when all allocated
        // items are part of the free list.
        if (m_items.empty ())
            m_free.clear ();

        size_type const used (m_items.size () + m_free.size ());
        size_type const handles ((used + m_blocksize - 1) / m_blocksize);
        m_capacity = handles * m_blocksize;
        for (size_type i = m_handles.size (); i-- > handles;)
        {
            std::free (m_handles [i]);
            m_handles.erase (m_handles.begin () + i);
        }
    }

    //--------------------------------------------------------------------------

    void clear ()
    {
        // Might want to skip this if is_pod<T> is true
        for (typename List <Item>::iterator iter = m_items.begin ();
            iter != m_items.end ();)
        {
            Item& item (*iter++);
            item.get ()->~T ();
            m_free.push_back (item);
        }
    }

    /** Allocate a new default-constructed element and return the iterator.
        If there are deleted elements in the free list, the new element
        may not be created at the end of the storage area.
    */
    iterator emplace_back ()
    {
        return iterator_to (*new (alloc ()->get ()) T ());
    }

    template <class A1>
    iterator emplace_back (A1 a1)
    {
        return iterator_to (*new (alloc ()->get ()) T (a1));
    }

    template <class A1, class A2>
    iterator emplace_back (A1 a1, A2 a2)
    {
        return iterator_to (*new (alloc ()->get ()) T (a1, a2));
    }

    template <class A1, class A2, class A3>
    iterator emplace_back (A1 a1, A2 a2, A3 a3)
    {
        return iterator_to (*new (alloc ()->get ()) T (a1, a2, a3));
    }

    template <class A1, class A2, class A3, class A4>
    iterator emplace_back (A1 a1, A2 a2, A3 a3, A4 a4)
    {
        return iterator_to (*new (alloc ()->get ()) T (a1, a2, a3, a4));
    }

    template <class A1, class A2, class A3, class A4, class A5>
    iterator emplace_back (A1 a1, A2 a2, A3 a3, A4 a4, A5 a5)
    {
        return iterator_to (*new (alloc ()->get ()) T (a1, a2, a3, a4, a5));
    }

    /** Allocate a new copy-constructed element and return the index. */
    iterator push_back (TParam value) noexcept
    {
        return iterator_to (*new (alloc ()->get ()) T (value));
    }

    /** Erase the element at the specified position. */
    iterator erase (iterator pos)
    {
        Item& item (*pos.m_iter);
        item.get ()->~T ();
        pos = m_items.erase (m_items.iterator_to (item));
        m_free.push_front (item);
        return pos;
    }

private:
    template <class, bool>
    friend class DynamicListIterator;

    struct Item : List <Item>::Node
    {
        typedef T value_type;

        T* get () noexcept
        {
            return reinterpret_cast <T*> (&storage [0]);
        }

        T const* get () const noexcept
        {
            return reinterpret_cast <T const*> (&storage [0]);
        }

    private:
        // Lets hope this is padded correctly
        uint8 storage [sizeof (T)];
    };

    Item* alloc () noexcept
    {
        Item* item;
        if (m_free.empty ())
        {
            if (m_capacity <= m_items.size ())
                reserve (m_items.size () + 1);

            size_type const index (m_items.size () / m_blocksize);
            size_type const offset (m_items.size () - index * m_blocksize);
            item = m_handles [index] + offset;
        }
        else
        {
            item = &m_free.pop_front ();
        }

        m_items.push_back (*item);
        return item;
    }

    typedef std::vector <Item*> blocks_t;

    Allocator m_allocator;
    size_type m_blocksize;
    size_type m_capacity;
    std::vector <Item*> m_handles;
    List <Item> m_items;
    List <Item> m_free;
};

#endif
