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

#ifndef BEAST_INTRUSIVE_LOCKFREESTACK_H_INCLUDED
#define BEAST_INTRUSIVE_LOCKFREESTACK_H_INCLUDED

#include <beast/Atomic.h>
#include <beast/Uncopyable.h>

#include <iterator>
#include <type_traits>

namespace beast {

//------------------------------------------------------------------------------

template <class Container, bool IsConst>
class LockFreeStackIterator
    : public std::iterator <
        std::forward_iterator_tag,
        typename Container::value_type,
        typename Container::difference_type,
        typename std::conditional <IsConst,
            typename Container::const_pointer,
            typename Container::pointer>::type,
        typename std::conditional <IsConst,
            typename Container::const_reference,
            typename Container::reference>::type>
{
protected:
    typedef typename Container::Node Node;
    typedef typename std::conditional <
        IsConst, Node const*, Node*>::type NodePtr;

public:
    typedef typename Container::value_type value_type;
    typedef typename std::conditional <IsConst,
        typename Container::const_pointer,
        typename Container::pointer>::type pointer;
    typedef typename std::conditional <IsConst,
        typename Container::const_reference,
        typename Container::reference>::type reference;

    LockFreeStackIterator ()
        : m_node ()
    {
    }

    LockFreeStackIterator (NodePtr node)
        : m_node (node)
    {
    }

    template <bool OtherIsConst>
    explicit LockFreeStackIterator (LockFreeStackIterator <Container, OtherIsConst> const& other)
        : m_node (other.m_node)
    {
    }

    LockFreeStackIterator& operator= (NodePtr node)
    {
        m_node = node;
        return static_cast <LockFreeStackIterator&> (*this);
    }

    LockFreeStackIterator& operator++ ()
    {
        m_node = m_node->m_next.get();
        return static_cast <LockFreeStackIterator&> (*this);
    }

    LockFreeStackIterator operator++ (int)
    {
        LockFreeStackIterator result (*this);
        m_node = m_node->m_next;
        return result;
    }

    NodePtr node() const
    {
        return m_node;
    }

    reference operator* () const
    {
        return *this->operator-> ();
    }

    pointer operator-> () const
    {
        return static_cast <pointer> (m_node);
    }

private:
    NodePtr m_node;
};

//------------------------------------------------------------------------------

template <class Container, bool LhsIsConst, bool RhsIsConst>
bool operator== (LockFreeStackIterator <Container, LhsIsConst> const& lhs,
                 LockFreeStackIterator <Container, RhsIsConst> const& rhs)
{
    return lhs.node() == rhs.node();
}

template <class Container, bool LhsIsConst, bool RhsIsConst>
bool operator!= (LockFreeStackIterator <Container, LhsIsConst> const& lhs,
                 LockFreeStackIterator <Container, RhsIsConst> const& rhs)
{
    return lhs.node() != rhs.node();
}

//------------------------------------------------------------------------------

/** Multiple Producer, Multiple Consumer (MPMC) intrusive stack.

    This stack is implemented using the same intrusive interface as List.
    All mutations are lock-free.

    The caller is responsible for preventing the "ABA" problem:
        http://en.wikipedia.org/wiki/ABA_problem

    @param Tag  A type name used to distinguish lists and nodes, for
                putting objects in multiple lists. If this parameter is
                omitted, the default tag is used.
*/
template <class Element, class Tag = void>
class LockFreeStack : public Uncopyable
{
public:
    class Node : public Uncopyable
    {
    public:
        Node ()
        {
        }

        explicit Node (Node* next) : m_next (next)
        {
        }

    private:
        friend class LockFreeStack;
        
        template <class Container, bool IsConst>
        friend class LockFreeStackIterator;

        Atomic <Node*> m_next;
    };

public:
    typedef Element                             value_type;
    typedef Element*                            pointer;
    typedef Element&                            reference;
    typedef Element const*                      const_pointer;
    typedef Element const&                      const_reference;
    typedef std::size_t                         size_type;
    typedef std::ptrdiff_t                      difference_type;
    typedef LockFreeStackIterator <
        LockFreeStack <Element, Tag>, false>    iterator;
    typedef LockFreeStackIterator <
        LockFreeStack <Element, Tag>, true>     const_iterator;

    LockFreeStack ()
        : m_end (nullptr)
        , m_head (&m_end)
    {
    }

    /** Returns true if the stack is empty. */
    bool empty() const
    {
        return m_head.get() == &m_end;
    }

    /** Push a node onto the stack.
        The caller is responsible for preventing the ABA problem.
        This operation is lock-free.
        Thread safety:
            Safe to call from any thread.

        @param node The node to push.

        @return `true` if the stack was previously empty. If multiple threads
                are attempting to push, only one will receive `true`.
    */
    // VFALCO NOTE Fix this, shouldn't it be a reference like intrusive list?
    bool push_front (Node* node)
    {
        bool first;
        Node* head;
        do
        {
            head = m_head.get ();
            first = head == &m_end;
            node->m_next = head;
        }
        while (!m_head.compareAndSetBool (node, head));
        return first;
    }

    /** Pop an element off the stack.
        The caller is responsible for preventing the ABA problem.
        This operation is lock-free.
        Thread safety:
            Safe to call from any thread.

        @return The element that was popped, or `nullptr` if the stack
                was empty.
    */
    Element* pop_front ()
    {
        Node* node;
        Node* head;
        do
        {
            node = m_head.get ();
            if (node == &m_end)
                return nullptr;
            head = node->m_next.get ();
        }
        while (!m_head.compareAndSetBool (head, node));
        return static_cast <Element*> (node);
    }

    /** Return a forward iterator to the beginning or end of the stack.
        Undefined behavior results if push_front or pop_front is called
        while an iteration is in progress.
        Thread safety:
            Caller is responsible for synchronization.
    */
    /** @{ */
    iterator begin ()
    {
        return iterator (m_head.get ());
    }

    iterator end ()
    {
        return iterator (&m_end);
    }
    
    const_iterator begin () const
    {
        return const_iterator (m_head.get ());
    }

    const_iterator end () const
    {
        return const_iterator (&m_end);
    }
    
    const_iterator cbegin () const
    {
        return const_iterator (m_head.get ());
    }

    const_iterator cend () const
    {
        return const_iterator (&m_end);
    }
    /** @} */

private:
    Node m_end;
    Atomic <Node*> m_head;
};

}

#endif
