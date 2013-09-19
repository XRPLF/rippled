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

#ifndef BEAST_LOCKFREESTACK_H_INCLUDED
#define BEAST_LOCKFREESTACK_H_INCLUDED

struct LockFreeStackDefaultTag;

/*============================================================================*/

template <class Container, bool IsConst>
class LockFreeStackIterator
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
    typedef typename Container::Node Node;
    typedef typename mpl::IfCond <IsConst, Node const*, Node*>::type NodePtr;

public:
    typedef typename Container::value_type value_type;
    typedef typename mpl::IfCond <IsConst,
        typename Container::const_pointer,
        typename Container::pointer>::type pointer;
    typedef typename mpl::IfCond <IsConst,
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
        m_node = m_node->m_next;
        return static_cast <LockFreeStackIterator&> (*this);
    }

    LockFreeStackIterator operator++ (int)
    {
        LockFreeStackIterator result (*this);
        m_node = m_node->m_next;
        return result;
    }

    template <bool OtherIsConst>
    bool operator== (LockFreeStackIterator <Container, OtherIsConst> const& other)
    {
        return m_node == other.m_node;
    }

    template <bool OtherIsConst>
    bool operator!= (LockFreeStackIterator <Container, OtherIsConst> const& other)
    {
        return m_node != other.m_node;
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

/**
  Multiple Producer, Multiple Consumer (MPMC) intrusive stack.

  This stack is implemented using the same intrusive interface as List. All
  operations are lock-free.

  The caller is responsible for preventing the "ABA" problem
  (http://en.wikipedia.org/wiki/ABA_problem)

  @param Tag  A type name used to distinguish lists and nodes, for
  putting objects in multiple lists. If this parameter is
  omitted, the default tag is used.

  @ingroup beast_core intrusive
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

        // VFALCO TODO Use regular Atomic<>
        AtomicPointer <Node> m_next;
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

    LockFreeStack () : m_head (nullptr)
    {
    }

#if 0
    /** Create a LockFreeStack from another stack.

        The contents of the other stack are atomically acquired.
        The other stack is cleared.

        @param other  The other stack to acquire.
    */
    explicit LockFreeStack (LockFreeStack& other)
    {
        Node* head;

        do
        {
            head = other.m_head.get ();
        }
        while (!other.m_head.compareAndSet (0, head));

        m_head = head;
    }
#endif

    /** Return the number of elements in the stack.
        
        Thread safety:
            Safe to call from any thread but the value may be inaccurate.
    */
    size_type size () const
    {
        return m_size.get ();
    }

    /** Push a node onto the stack.

        The caller is responsible for preventing the ABA problem. This operation
        is lock-free.

        @param node The node to push.

        @return     True if the stack was previously empty. If multiple threads
                    are attempting to push, only one will receive true.
    */
    bool push_front (Node* node)
    {
        bool first;
        Node* head;

        do
        {
            head = m_head.get ();
            first = head == 0;
            node->m_next = head;
        }
        while (!m_head.compareAndSet (node, head));

        ++m_size;

        return first;
    }

    /** Pop an element off the stack.

        The caller is responsible for preventing the ABA problem. This operation
        is lock-free.

        @return   The element that was popped, or nullptr if the stack was empty.
    */
    Element* pop_front ()
    {
        Node* node;
        Node* head;

        do
        {
            node = m_head.get ();

            if (node == 0)
                break;

            head = node->m_next.get ();
        }
        while (!m_head.compareAndSet (head, node));

        --m_size;

        return node ? static_cast <Element*> (node) : nullptr;
    }

    /** Swap the contents of this stack with another stack.

        This call is not thread safe or atomic. The caller is responsible for
        synchronizing access.

        @param other  The other stack to swap contents with.
    */
    void swap (LockFreeStack& other)
    {
        Node* temp = other.m_head.get ();
        other.m_head.set (m_head.get ());
        m_head.set (temp);

        std::swap (m_size.value, other.m_size.value);
    }

    iterator begin ()
    {
        return iterator (m_head.get ());
    }

    iterator end ()
    {
        return iterator ();
    }
    
    const_iterator begin () const
    {
        return const_iterator (m_head.get ());
    }

    const_iterator end () const
    {
        return const_iterator ();
    }
    
    const_iterator cbegin () const
    {
        return const_iterator (m_head.get ());
    }

    const_iterator cend () const
    {
        return const_iterator ();
    }
    
private:
    Atomic <size_type> m_size;
    AtomicPointer <Node> m_head;
};

#endif
