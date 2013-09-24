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

#ifndef BEAST_CORE_CONTAINERS_DYNAMICARRAY_H_INCLUDED
#define BEAST_CORE_CONTAINERS_DYNAMICARRAY_H_INCLUDED

template <typename, typename>
class DynamicArray;

namespace detail
{

template <typename V>
class DynamicArrayIterator
    : public std::iterator <std::random_access_iterator_tag,
        typename V::size_type>
{
public:
    typedef typename mpl::CopyConst <V, typename V::value_type>::type

                              value_type;
    typedef value_type*       pointer;
    typedef value_type&       reference;
    typedef std::ptrdiff_t    difference_type;
    typedef typename V::size_type size_type;

    DynamicArrayIterator (V* v = nullptr, size_type pos = 0) noexcept
        : m_v (v)
        , m_pos (pos)
    {
    }

    template <typename W>
    DynamicArrayIterator (DynamicArrayIterator <W> const& u) noexcept
        : m_v (u.m_v)
        , m_pos (u.m_pos)
    {
    }

    template <typename W>
    DynamicArrayIterator& operator= (DynamicArrayIterator <W> const& u) noexcept
    {
        m_v = u.m_v;
        m_pos = u.m_pos;
        return *this;
    }

    template <typename W>
    bool operator== (DynamicArrayIterator <W> const& u) const noexcept
    {
        return (m_v == u.m_v) && (m_pos == u.m_pos);
    }

    template <typename W>
    bool operator!= (DynamicArrayIterator <W> const& u) const noexcept
    {
        return ! ((*this) == u);
    }

    reference operator* () const noexcept
    {
        return dereference ();
    }

    pointer operator-> () const noexcept
    {
        return &dereference ();
    }

    DynamicArrayIterator& operator++ () noexcept
    {
        increment (1);
        return *this;
    }

    DynamicArrayIterator operator++ (int) noexcept
    {
        DynamicArrayIterator const result (*this);
        increment (1);
        return result;
    }

    DynamicArrayIterator& operator-- () noexcept
    {
        decrement (1);
        return *this;
    }

    DynamicArrayIterator operator-- (int) noexcept
    {
        DynamicArrayIterator const result (*this);
        decrement (1);
        return result;
    }

    DynamicArrayIterator& operator+= (difference_type n) noexcept
    {
        increment (n);
        return *this;
    }

    DynamicArrayIterator& operator-= (difference_type n) noexcept
    {
        decrement (n);
        return *this;
    }

    DynamicArrayIterator operator+ (difference_type n) noexcept
    {
        return DynamicArrayIterator (m_v, m_pos + n);
    }

    DynamicArrayIterator operator- (difference_type n) noexcept
    {
        return DynamicArrayIterator (m_v, m_pos - n);
    }

    template <typename W>
    difference_type operator- (DynamicArrayIterator <W> const& rhs) const noexcept
    {
        return m_pos - rhs.m_pos;
    }

    template <typename W>
    bool operator< (DynamicArrayIterator <W> const& rhs) const noexcept
    {
        return m_pos < rhs.m_pos;
    }

    template <typename W>
    bool operator> (DynamicArrayIterator <W> const& rhs) const noexcept
    {
        return m_pos > rhs.m_pos;
    }

    template <typename W>
    bool operator<= (DynamicArrayIterator <W> const& rhs) const noexcept
    {
        return m_pos <= rhs.m_pos;
    }

    template <typename W>
    bool operator>= (DynamicArrayIterator <W> const& rhs) const noexcept
    {
        return m_pos >= rhs.m_pos;
    }

    reference operator[] (difference_type n) noexcept
    {
        return (*m_v)[m_pos + n];
    }

private:
    reference dereference () const noexcept
    {
        return (*m_v) [m_pos];
    }

    void increment (difference_type n) noexcept
    {
        m_pos += n;
    }

    void decrement (difference_type n) noexcept
    {
        m_pos -= n;
    }

private:
    template <typename>
    friend class DynamicArrayIterator;

    V* m_v;
    size_type m_pos;
};

//------------------------------------------------------------------------------

template <typename V>
DynamicArrayIterator <V> operator+ (
    typename DynamicArrayIterator <V>::difference_type n,
        DynamicArrayIterator <V> iter) noexcept
{
    return iter + n;
}

template <typename V>
DynamicArrayIterator <V> operator- (
    typename DynamicArrayIterator <V>::difference_type n,
        DynamicArrayIterator <V> iter) noexcept
{
    return iter - n;
}

//------------------------------------------------------------------------------

template <typename V>
class DynamicArrayReverseIterator
    : public std::iterator <std::random_access_iterator_tag,
        typename V::size_type>
{
public:
    typedef typename mpl::CopyConst<V, typename V::value_type>::type

                              value_type;
    typedef value_type*       pointer;
    typedef value_type&       reference;
    typedef std::ptrdiff_t    difference_type;
    typedef typename V::size_type size_type;

    DynamicArrayReverseIterator (V* v = nullptr, difference_type pos = 0) noexcept
        : m_v (v)
        , m_pos (pos)
    {
    }

    template <typename W>
    DynamicArrayReverseIterator (DynamicArrayReverseIterator <W> const& u) noexcept
        : m_v (u.m_v)
        , m_pos (u.m_pos)
    {
    }

    template <typename W>
    DynamicArrayReverseIterator& operator= (DynamicArrayReverseIterator <W> const& u) noexcept
    {
        m_v = u.m_v;
        m_pos = u.m_pos;
        return *this;
    }

    template <typename W>
    bool operator== (DynamicArrayReverseIterator <W> const& u) const noexcept
    {
        return (m_v == u.m_v) && (m_pos == u.m_pos);
    }

    template <typename W>
    bool operator!= (DynamicArrayReverseIterator <W> const& u) const noexcept
    {
        return ! ((*this) == u);
    }

    reference operator* () const noexcept
    {
        return dereference ();
    }

    pointer operator-> () const noexcept
    {
        return &dereference ();
    }

    DynamicArrayReverseIterator& operator++ () noexcept
    {
        increment (1);
        return *this;
    }

    DynamicArrayReverseIterator operator++ (int) noexcept
    {
        DynamicArrayReverseIterator const result (*this);
        increment (1);
        return result;
    }

    DynamicArrayReverseIterator& operator-- () noexcept
    {
        decrement (1);
        return *this;
    }

    DynamicArrayReverseIterator operator-- (int) noexcept
    {
        DynamicArrayReverseIterator const result (*this);
        decrement (1);
        return result;
    }

    DynamicArrayReverseIterator& operator+= (difference_type n) noexcept
    {
        increment (n);
        return *this;
    }

    DynamicArrayReverseIterator& operator-= (difference_type n) noexcept
    {
        decrement (n);
        return *this;
    }

    DynamicArrayReverseIterator operator+ (difference_type n) noexcept
    {
        return DynamicArrayReverseIterator (m_v, m_pos - n);
    }

    DynamicArrayReverseIterator operator- (difference_type n) noexcept
    {
        return DynamicArrayReverseIterator (m_v, m_pos + n);
    }

    template <typename W>
    difference_type operator- (DynamicArrayReverseIterator <W> const& rhs) const noexcept
    {
        return rhs.m_pos - m_pos;
    }

    template <typename W>
    bool operator< (DynamicArrayReverseIterator <W> const& rhs) const noexcept
    {
        return m_pos > rhs.m_pos;
    }

    template <typename W>
    bool operator> (DynamicArrayReverseIterator <W> const& rhs) const noexcept
    {
        return m_pos < rhs.m_pos;
    }

    template <typename W>
    bool operator<= (DynamicArrayReverseIterator <W> const& rhs) const noexcept
    {
        return m_pos >= rhs.m_pos;
    }

    template <typename W>
    bool operator>= (DynamicArrayReverseIterator <W> const& rhs) const noexcept
    {
        return m_pos <= rhs.m_pos;
    }

    reference operator[] (difference_type n) noexcept
    {
        return (*m_v)[(m_pos - 1) - n];
    }

private:
    template <typename>
    friend class DynamicArrayReverseIterator;

    reference dereference () const noexcept
    {
        return (*m_v) [m_pos - 1];
    }

    void increment (difference_type n) noexcept
    {
        m_pos -= n;
    }

    void decrement (difference_type n) noexcept
    {
        m_pos += n;
    }

    V* m_v;
    difference_type m_pos;
};

//------------------------------------------------------------------------------

template <typename V>
DynamicArrayReverseIterator <V> operator+ (
    typename DynamicArrayReverseIterator <V>::difference_type n,
        DynamicArrayReverseIterator <V> iter) noexcept
{
    return iter + n;
}

template <typename V>
DynamicArrayReverseIterator <V> operator- (
    typename DynamicArrayReverseIterator <V>::difference_type n,
        DynamicArrayReverseIterator <V> iter) noexcept
{
    return iter - n;
}

}

//------------------------------------------------------------------------------

template <typename T,
          typename Allocator = std::allocator <char> >
class DynamicArray
{
private:
    typedef PARAMETER_TYPE (T) TParam;

    typedef std::vector <T*> handles_t;

public:
    enum
    {
        defaultBlocksize = 1000,
        growthPercentage = 10
    };

    typedef T                 value_type;
    typedef Allocator         allocator_type;
    typedef std::size_t       size_type;
    typedef std::ptrdiff_t    difference_type;
    typedef value_type*       pointer;
    typedef value_type&       reference;
    typedef value_type const* const_pointer;
    typedef value_type const& const_reference;

    typedef detail::DynamicArrayIterator <DynamicArray <T> > iterator;

    typedef detail::DynamicArrayIterator <DynamicArray <T> const> const_iterator;

    typedef detail::DynamicArrayReverseIterator <DynamicArray <T> > reverse_iterator;

    typedef detail::DynamicArrayReverseIterator <DynamicArray <T> const> const_reverse_iterator;

    //--------------------------------------------------------------------------

    explicit DynamicArray (size_type blocksize = defaultBlocksize) noexcept
        : m_blocksize (blocksize)
        , m_capacity (0)
        , m_size (0)
    {
    }

    ~DynamicArray()
    {
        clear ();
        shrink_to_fit ();
    }

    /** Replace the array with 'count' copies of a default-constructed T.
    */
    void assign (size_type count)
    {
        clear ();
        resize (count);
    }

    //--------------------------------------------------------------------------

    reference at (size_type pos)
    {
        if (pos >= size ())
            Throw (std::out_of_range ("bad pos"), __FILE__, __LINE__);
        return get (pos);
    }

    const_reference at (size_type pos) const
    {
        if (pos >= size ())
            Throw (std::out_of_range ("bad pos"), __FILE__, __LINE__);
        return get (pos);
    }

    reference operator[] (size_type pos) noexcept
    {
        return get (pos);
    }

    const_reference operator[] (size_type pos) const noexcept
    {
        return get (pos);
    }

    reference front () noexcept
    {
        return get (0);
    }

    const_reference front () const noexcept
    {
        return get (0);
    }

    reference back () noexcept
    {
        return get (size () - 1);
    }

    const_reference back () const noexcept
    {
        return get (size () - 1);
    }

    //--------------------------------------------------------------------------

    iterator begin () noexcept
    {
        return iterator (this, 0);
    }

    const_iterator begin () const noexcept
    {
        return const_iterator (this, 0);
    }

    const_iterator cbegin () const noexcept
    {
        return const_iterator (this, 0);
    }

    iterator end () noexcept
    {
        return iterator (this, size ());
    }

    const_iterator end () const noexcept
    {
        return const_iterator (this, size ());
    }

    const_iterator cend () const noexcept
    {
        return const_iterator (this, size ());
    }

    reverse_iterator rbegin () noexcept
    {
        return reverse_iterator (this, size ());
    }

    const_reverse_iterator rbegin () const noexcept
    {
        return const_reverse_iterator (this, size ());
    }

    const_reverse_iterator crbegin () const noexcept
    {
        return const_reverse_iterator (this, size ());
    }

    reverse_iterator rend () noexcept
    {
        return reverse_iterator (this, 0);
    }

    const_reverse_iterator rend () const noexcept
    {
        return const_reverse_iterator (this, 0);
    }

    const_reverse_iterator crend () const noexcept
    {
        return const_reverse_iterator (this, 0);
    }

    //--------------------------------------------------------------------------

    bool empty () const noexcept
    {
        return m_size == 0;
    }

    size_type size () const noexcept
    {
        return m_size;
    }

    size_type max_size () const noexcept
    {
        return std::numeric_limits <size_type>::max ();
    }

    void reserve (size_type new_cap)
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
            m_handles.push_back (static_cast <T*> (std::malloc (
                m_blocksize * sizeof (T))));
        m_capacity = new_cap;
    }

    size_type capacity () const noexcept
    {
        return m_capacity;
    }

    void shrink_to_fit ()
    {
        size_type const handles (
            (size () + m_blocksize - 1) / m_blocksize);
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
        resize (0);
    }

    iterator push_back (TParam value)
    {
        new (alloc ()) T (value);
        return iterator (this, size () - 1);
    }

    iterator emplace_back ()
    {
        new (alloc ()) T ();
        return iterator (this, size () - 1);
    }

    template <class A1>
    iterator emplace_back (A1 a1)
    {
        new (alloc ()) T (a1);
        return iterator (this, size () - 1);
    }

    template <class A1, class A2>
    iterator emplace_back (A1 a1, A2 a2)
    {
        new (alloc ()) T (a1, a2);
        return iterator (this, size () - 1);
    }
    
    template <class A1, class A2, class A3>
    iterator emplace_back (A1 a1, A2 a2, A3 a3)
    {
        new (alloc ()) T (a1, a2, a3);
        return iterator (this, size () - 1);
    }
    
    template <class A1, class A2, class A3, class A4>
    iterator emplace_back (A1 a1, A2 a2, A3 a3, A4 a4)
    {
        new (alloc ()) T (a1, a2, a3, a4);
        return iterator (this, size () - 1);
    }

    template <class A1, class A2, class A3, class A4, class A5>
    iterator emplace_back (A1 a1, A2 a2, A3 a3, A4 a4, A5 a5)
    {
        new (alloc ()) T (a1, a2, a3, a4, a5);
        return iterator (this, size () - 1);
    }
   
    void pop_back ()
    {
        resize (size () - 1);
    }

    void resize (size_type count)
    {
        while (count > size ())
            new (alloc ()) T;

        while (count < size ())
            get (--m_size).~T ();
    }

    void resize (size_type count, TParam value)
    {
        while (count > size ())
            new (alloc ()) T (value);

        while (count < size ())
            get (--m_size).~T ();
    }

    void swap (DynamicArray& other)
    {
        std::swap (m_blocksize, other.m_blocksize);
        std::swap (m_size,      other.m_size);
        std::swap (m_capacity,  other.m_capacity);
        std::swap (m_handles,   other.m_handles);
    }

private:
    reference get (size_type pos) noexcept
    {
        size_type const index (pos / m_blocksize);
        size_type const offset (pos % m_blocksize);
        return m_handles [index] [offset];
    }

    const_reference get (size_type pos) const noexcept
    {
        size_type const index (pos / m_blocksize);
        size_type const offset (pos % m_blocksize);
        return m_handles [index] [offset];
    }

    T* alloc () noexcept
    {
        size_type const needed (size () + 1);
        if (capacity () < needed)
            reserve ((needed * (100 + growthPercentage) + 99) / 100);
        return &get (m_size++);
    }

private:
    Allocator m_allocator;
    size_type m_blocksize;
    size_type m_capacity;
    size_type m_size;
    handles_t m_handles;
};

#endif
