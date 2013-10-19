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

#ifndef BEAST_INTRUSIVE_FORWARDLIST_H_INCLUDED
#define BEAST_INTRUSIVE_FORWARDLIST_H_INCLUDED

#include "../Config.h"

#include "PointerTraits.h"

#include "../MPL.h"

#include <iterator>

// Ideas based on boost

namespace beast {
namespace intrusive {

//------------------------------------------------------------------------------

namespace detail {

// Holds the size field
struct SizeHolder
{
public:
    typedef std::size_t size_type;
   
    inline size_type size () const noexcept
    {
        return m_size;
    }

    inline void set (size_type new_size) noexcept
    {
        m_size = new_size;
    }

    inline void increment () noexcept
    {
        ++m_size;
    }

    inline void decrement () noexcept
    {
        --m_size;
    }

private:
    size_type m_size;
};

}

//------------------------------------------------------------------------------

template <class VoidPointer>
struct ForwardListNode
{
    typedef typename PointerTraits <VoidPointer>::template rebind_pointer <ForwardListNode>::type node_ptr;

    node_ptr next;
};

//------------------------------------------------------------------------------

// Provides value_traits for when T derives from Node
template <class T, class NodeTraits>
struct DerivedValueTraits
{
    typedef NodeTraits                                             node_traits;
    typedef T                                                      value_type;
    typedef typename node_traits::node                             node;
    typedef typename node_traits::node_ptr                         node_ptr;
    typedef typename node_traits::const_node_ptr                   const_node_ptr;
    typedef typename mpl::PointerToOther <node_ptr, T>::type       pointer;
    typedef typename mpl::PointerToOther <node_ptr, const T>::type const_pointer;
    typedef typename PointerTraits <pointer>::reference            reference;
    typedef typename PointerTraits <const_pointer>::reference      const_reference;

    static node_ptr to_node_ptr (reference value)
    {
        return node_ptr (&value);
    }

    static const_node_ptr to_node_ptr (const_reference value)
    {
        return node_ptr (&value);
    }

    static pointer to_value_ptr (node_ptr const& n)
    {
        return pointer (&static_cast <value_type&> (*n));
    }

    static const_pointer to_value_ptr (const_node_ptr const &n)
    {
        return const_pointer (&static_cast <value_type const&> (*n));
    }
};

//------------------------------------------------------------------------------

template <class VoidPointer, typename Tag>
struct ForwardListNodeTraits
{
    typedef ForwardListNode <VoidPointer> node;

    typedef typename PointerTraits <VoidPointer>::
        template rebind_pointer <node> node_ptr;

    typedef typename PointerTraits <VoidPointer>::
        template rebind_pointer <node const> const_node_ptr;

    static node_ptr get_next (const_node_ptr const& n)
    {
        return n->m_next;
    }

    static node_ptr get_next (node_ptr const& n)
    {
        return n->m_next;
    }

    static void set_next (node_ptr const& n, node_ptr const& next)
    {
        n->m_next = next;
    }
};

//------------------------------------------------------------------------------

template <class Container, bool IsConst>
class ForwardListIterator
    : public std::iterator <
        std::forward_iterator_tag,
        typename Container::value_type,
        typename Container::difference_type,
        typename mpl::IfCond <IsConst,
            typename Container::const_pointer,
            typename Container::pointer>::type,
        typename mpl::IfCond <IsConst,
            typename Container::const_reference,
            typename Container::reference>::type>
{
protected:
    typedef typename Container::value_traits value_traits;
    typedef typename Container::node_traits  node_traits;
    typedef typename node_traits::node       node;
    typedef typename node_traits::node_ptr   node_ptr;
    typedef typename PointerTraits <node_ptr>::
        template rebind_pointer <void>::type void_pointer;

public:
    typedef typename Container::value_type value_type;
    typedef typename mpl::IfCond <IsConst,
        typename Container::const_pointer,
        typename Container::pointer>::type pointer;
    typedef typename mpl::IfCond <IsConst,
        typename Container::const_reference,
        typename Container::reference>::type reference;

    ForwardListIterator ()
        : m_node ()
    {
    }

    explicit ForwardListIterator (ForwardListIterator <Container, false> const& other)
        : m_node (other.pointed_node ())
    {
    }

    node_ptr const& pointed_node () const noexcept
    {
        return m_node;
    }

    ForwardListIterator& operator= (node_ptr const& node)
    {
        m_node = node;
        return static_cast <ForwardListIterator&> (*this);
    }

    ForwardListIterator& operator++ ()
    {
        m_node = node_traits::get_next (m_node);
        return static_cast <ForwardListIterator&> (*this);
    }

    ForwardListIterator operator++ (int)
    {
        ForwardListIterator result (*this);
        m_node = node_traits::get_next (m_node);
        return result;
    }

    friend bool operator== (ForwardListIterator const& lhs,
        ForwardListIterator const& rhs)
    {
        return lhs.m_node == rhs.m_node;
    }

    friend bool operator!= (ForwardListIterator const& lhs,
        ForwardListIterator const& rhs)
    {
        return ! (lhs == rhs);
    }

    reference operator* () const
    {
        return *this->operator-> ();
    }

    pointer operator-> () const
    {
        return value_traits::to_value_ptr (m_node);
    }

private:
    node_ptr m_node;
};

//------------------------------------------------------------------------------

template <class NodeTraits>
class ForwardListAlgorithms
{
public:
    typedef typename NodeTraits::node           node;
    typedef typename NodeTraits::node_ptr       node_ptr;
    typedef typename NodeTraits::const_node_ptr const_node_ptr;
    typedef NodeTraits                          node_traits;

    static void init (node_ptr const& n)
    {
        NodeTraits::set_next (n, node_ptr());
    }

    static bool unique (const_node_ptr const& this_node)
    {
        node_ptr next = NodeTraits::get_next (this_node);
        return !next || next == this_node;
    }

    static void link_after (node_ptr const& prev_node, node_ptr const& this_node)
    {
        NodeTraits::set_next (this_node, NodeTraits::get_next (prev_node));
        NodeTraits::set_next (prev_node, this_node);
    }

    static void unlink_after (node_ptr const& prev_node)
    {
        const_node_ptr this_node (NodeTraits::get_next (prev_node));
        NodeTraits::set_next (prev_node, NodeTraits::get_next (this_node));
    }
};

//------------------------------------------------------------------------------

/** Singly-linked intrusive list. */
template <typename T, typename Tag = void>
class ForwardList
{
public:
    typedef DerivedValueTraits <T, ForwardListNodeTraits <T, Tag> >
                                                              value_traits;
    typedef typename value_traits::pointer                    pointer;
    typedef typename value_traits::const_pointer              const_pointer;
    typedef typename PointerTraits <pointer>::element_type    value_type;
    typedef typename PointerTraits <pointer>::reference       reference;
    typedef typename PointerTraits <pointer>::const_reference const_reference;
    typedef typename PointerTraits <pointer>::difference_type difference_type;
    typedef std::size_t                                       size_type;
    typedef ForwardListIterator <ForwardList, false>          iterator;
    typedef ForwardListIterator <ForwardList, true>           const_iterator;
    typedef typename value_traits::node_traits                node_traits;
    typedef typename node_traits::node                        node;
    typedef typename node_traits::node_ptr                    node_ptr;
    typedef typename node_traits::const_node_ptr              const_node_ptr;
    typedef ForwardListAlgorithms <node_traits>               node_algorithms;

    typedef node Node;

private:
    typedef detail::SizeHolder size_traits;

    void default_construct ()
    {
        get_size_traits ().set (size_type (0));
        node_algorithms::init (this->get_root_node ());
    }

    node_ptr get_end_node ()
    {
        return node_ptr ();
    }

    const_node_ptr get_end_node () const
    {
        return const_node_ptr ();
    }

    node_ptr get_root_node ()
    {
        return PointerTraits <node_ptr>::pointer_to (m_root);
    }

    const_node_ptr get_root_node () const
    {
        return PointerTraits <const_node_ptr>::pointer_to (m_root);
    }

    size_traits& get_size_traits () noexcept
    {
        return m_size;
    }

    size_traits const& get_size_traits () const noexcept
    {
        return m_size;
    }

    static node_ptr uncast (const_node_ptr const& ptr)
    {
        return PointerTraits <node_ptr>::const_cast_from (ptr);
    }

public:
    ForwardList ()
    {
        default_construct ();
    }

    void clear ()
    {
        default_construct ();
    }

    void push_front (reference value)
    {
        node_ptr this_node (value_traits::to_node_ptr (value));
        node_algorithms::link_after (this->get_root_node (), this_node);
        this->get_size_traits ().increment ();
    }

    void pop_front ()
    {
        //node_ptr this_node (node_traits::get_next (this->get_root ()));
        node_algorithms::unlink_after (this->get_root_node ());
        this->get_size_traits ().decrement ();
    }

    reference front ()
    {
        return *value_traits::to_value_ptr (node_traits::get_next (this->get_root_node ()));
    }

    const_reference front () const
    {
        return *value_traits::to_value_ptr (uncat (node_traits::get_next (this->get_root_node ())));
    }

    iterator begin ()
    {
        return iterator (node_traits::get_next (this->get_root_node (), this));
    }

    const_iterator begin () const
    {
        return const_iterator (node_traits::get_next (this->get_root_node (), this));
    }

    const_iterator cbegin () const
    {
        return this->begin ();
    }

    iterator end ()
    {
        return iterator (this->get_end_node (), this);
    }

    const_iterator end () const
    {
        return const_iterator (this->get_end_node (), this);
    }

    const_iterator cend () const
    {
        return this->end ();
    }

    iterator before_begin ()
    {
        return iterator (this->get_root_node (), this);
    }

    const_iterator before_begin () const
    {
        return const_iterator (this->get_root_node (), this);
    }

    const_iterator cbefore_begin () const
    {
        return before_begin ();
    }

    bool empty () const
    {
        return node_algorithms::unique (this->get_root_node ());
    }

    iterator iterator_to (reference value)
    {
        return iterator (value_traits::to_node_ptr (value), this);
    }

    const_iterator iterator_to (const_reference value) const
    {
        return const_iterator (value_traits::to_node_ptr (const_cast <reference> (value)), this);
    }

private:
    node m_root;
    size_traits m_size;
};

}
}

#endif
