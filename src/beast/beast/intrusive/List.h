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

#ifndef BEAST_INTRUSIVE_LIST_H_INCLUDED
#define BEAST_INTRUSIVE_LIST_H_INCLUDED

#include <iterator>
#include "../mpl/CopyConst.h"
#include "../Uncopyable.h"

namespace beast {

/** Intrusive Containers

    # Introduction

    Intrusive containers are special containers that offer better performance
    and exception safety guarantees than non-intrusive containers (like the
    STL containers). They are useful building blocks for high performance
    concurrent systems or other purposes where allocations are restricted
    (such as the AudioIODeviceCallback object), because intrusive list
    operations do not allocate or free memory.

    While intrusive containers were and are widely used in C, they became more
    and more forgotten in C++ due to the presence of the standard containers
    which don't support intrusive techniques. VFLib not only reintroduces this
    technique to C++ for lists, it also encapsulates the implementation in a
    mostly compliant STL interface. Hence anyone familiar with standard
    containers can easily use them.

    # Interface

    The interface for intrusive elements in this library is unified for all
    containers. Unlike STL containers, objects placed into intrusive containers
    are not copied. Instead, a pointer to the object is stored. All
    responsibility for object lifetime is the responsibility of the caller;
    the intrusive container just keeps track of what is in it.

    Summary of intrusive container differences:

    - Holds pointers to existing objects instead of copies.

    - Does not allocate or free any objects.

    - Requires a element's class declaration to be modified.

    - Methods never throw exceptions when called with valid arguments.

    # Usage

    Like STL containers, intrusive containers are all template based, where the
    template argument specifies the type of object that the container will hold.
    These declarations specify a doubly linked list where each element points
    to a user defined class:

    @code

    struct Object; // Forward declaration

    List <Object> list; // Doubly-linked list of Object

    @endcode

    Because intrusive containers allocate no memory, allowing objects to be
    placed inside requires a modification to their class declaration. Each
    intrusive container declares a nested class `Node` which elements must be
    derived from, using the Curiously Recurring Template Pattern (CRTP). We
    will continue to fully declare the Object type from the previous example
    to support emplacement into an intrusive container:

    @code

    struct Object : public List <Object>::Node // Required for List
    {
        void performAction ();
    };

    @endcode

    Usage of a typedef eliminates redundant specification of the template
    arguments but requires a forward declaration. The following code is
    equivalent.

    @code

    struct Object; // Forward declaration

    // Specify template parameters just once
    typedef List <Object> ListType;

    struct Object : public ListType::Node
    {
        void performAction ();
    };

    ListType::Node list;

    @endcode

    With these declarations we may proceed to create our objects, add them to
    the list, and perform operations:

    @code

    // Create a few objects and put them in the list
    for (i = 0; i < 5; ++i)
        list.push_back (*new Object);

    // Call a method on each list
    for (ListType::iterator iter = list.begin(); iter != list.end (); ++iter)
        iter->performAction ();

    @endcode

    Unlike regular STL containers, an object derived from an intrusive container
    node cannot exist in more than one instance of that list at a time. This is
    because the bookkeeping information for maintaining the list is kept in
    the object rather than the list.

    To support objects existing in multiple containers, templates variations
    are instantiated by distinguishing them with an empty structure, called a
    tag. The object is derived from multiple instances of Node, where each
    instance specifies a unique tag. The tag is passed as the second template
    argument. When the second argument is unspecified, the default tag is used.

    This declaration example shows the usage of tags to allow an object to exist
    simultaneously in two separate lists:

    @code

    struct GlobalListTag { }; // list of all objects
    struct ActiveListTag { }; // subset of all objects that are active

    class Object : public List <Object, GlobalListTag>
                , public List <Object, ActiveListTag>
    {
    public:
    Object () : m_isActive (false)
    {
        // Add ourselves to the global list
        s_globalList.push_front (*this);
    }

    ~Object ()
    {
        deactivate ();
    }

    void becomeActive ()
    {
        // Add ourselves to the active list
        if (!m_isActive)
        {
            s_activeList.push_front (*this);
            m_isActive = true;
        }
    }

    void deactivate ()
    {
        if (m_isActive)
        {
            // Doesn't delete the object
            s_activeList.erase (s_activeList.iterator_to (this));

            m_isActive = false;
        }
    }

    private:
        bool m_isActive;

        static List <Object, GlobalListTag> s_globalList;
        static List <Object, ActiveListTag> s_activeList;
    }

    @endcode

    @defgroup intrusive intrusive
    @ingroup beast_core
*/
 
//------------------------------------------------------------------------------

template <typename, typename>
class List;

namespace detail
{

// This is the intrusive portion of the doubly linked list.
// One derivation per list that the object may appear on
// concurrently is required.
//
template <typename T, typename Tag>
class ListNode : public Uncopyable
{
private:
    typedef T value_type;

    template <typename, typename>
    friend class List;

    template <typename>
    friend class ListIterator;

    ListNode* m_next;
    ListNode* m_prev;
};

//------------------------------------------------------------------------------

template <typename N>
class ListIterator : public std::iterator <
    std::bidirectional_iterator_tag, std::size_t>
{
public:
    typedef typename mpl::CopyConst<N, typename N::value_type>::type
                                   value_type;
    typedef value_type*            pointer;
    typedef value_type&            reference;
    typedef std::size_t            size_type;

    ListIterator (N* node = nullptr) noexcept
        : m_node (node)
    {
    }

    template <typename M>
    ListIterator (ListIterator <M> const& other) noexcept
        : m_node (other.m_node)
    {
    }

    template <typename M>
    bool operator== (ListIterator <M> const& other) const noexcept
    {
        return m_node == other.m_node;
    }

    template <typename M>
    bool operator!= (ListIterator <M> const& other) const noexcept
    {
        return ! ((*this) == other);
    }

    reference operator* () const noexcept
    {
        return dereference ();
    }

    pointer operator-> () const noexcept
    {
        return &dereference ();
    }

    ListIterator& operator++ () noexcept
    {
        increment ();
        return *this;
    }

    ListIterator operator++ (int) noexcept
    {
        ListIterator result (*this);
        increment ();
        return result;
    }

    ListIterator& operator-- () noexcept
    {
        decrement ();
        return *this;
    }

    ListIterator operator-- (int) noexcept
    {
        ListIterator result (*this);
        decrement ();
        return result;
    }

private:
    reference dereference () const noexcept
    {
        return static_cast <reference> (*m_node);
    }

    void increment () noexcept
    {
        m_node = m_node->m_next;
    }

    void decrement () noexcept
    {
        m_node = m_node->m_prev;
    }

    N* m_node;
};

}

/** Intrusive doubly linked list.
  
    This intrusive List is a container similar in operation to std::list in the
    Standard Template Library (STL). Like all @ref intrusive containers, List
    requires you to first derive your class from List<>::Node:
  
    @code
  
    struct Object : List <Object>::Node
    {
        explicit Object (int value) : m_value (value)
        {
        }
  
        int m_value;
    };
  
    @endcode
  
    Now we define the list, and add a couple of items.
  
    @code
  
    List <Object> list;
  
    list.push_back (* (new Object (1)));
    list.push_back (* (new Object (2)));
  
    @endcode
  
    For compatibility with the standard containers, push_back() expects a
    reference to the object. Unlike the standard container, however, push_back()
    places the actual object in the list and not a copy-constructed duplicate.
  
    Iterating over the list follows the same idiom as the STL:
  
    @code
  
    for (List <Object>::iterator iter = list.begin(); iter != list.end; ++iter)
        std::cout << iter->m_value;
  
    @endcode
  
    You can even use BOOST_FOREACH, or range based for loops:
  
    @code
  
    BOOST_FOREACH (Object& object, list)  // boost only
        std::cout << object.m_value;
  
    for (Object& object : list)           // C++11 only
        std::cout << object.m_value;
  
    @endcode
  
    Because List is mostly STL compliant, it can be passed into STL algorithms:
    e.g. `std::for_each()` or `std::find_first_of()`.
  
    In general, objects placed into a List should be dynamically allocated
    although this cannot be enforced at compile time. Since the caller provides
    the storage for the object, the caller is also responsible for deleting the
    object. An object still exists after being removed from a List, until the
    caller deletes it. This means an element can be moved from one List to
    another with practically no overhead.
  
    Unlike the standard containers, an object may only exist in one list at a
    time, unless special preparations are made. The Tag template parameter is
    used to distinguish between different list types for the same object,
    allowing the object to exist in more than one list simultaneously.
  
    For example, consider an actor system where a global list of actors is
    maintained, so that they can each be periodically receive processing
    time. We wish to also maintain a list of the subset of actors that require
    a domain-dependent update. To achieve this, we declare two tags, the
    associated list types, and the list element thusly:
  
    @code
  
    struct Actor;         // Forward declaration required
  
    struct ProcessTag { };
    struct UpdateTag { };
  
    typedef List <Actor, ProcessTag> ProcessList;
    typedef List <Actor, UpdateTag> UpdateList;
  
    // Derive from both node types so we can be in each list at once.
    //
    struct Actor : ProcessList::Node, UpdateList::Node
    {
        bool process ();    // returns true if we need an update
        void update ();
    };
  
    @endcode
  
    @tparam T The base type of element which the list will store
                    pointers to.
  
    @tparam Tag An optional unique type name used to distinguish lists and nodes,
                when the object can exist in multiple lists simultaneously.
  
    @ingroup beast_core intrusive
*/
template <typename T, typename Tag = void>
class List : public Uncopyable
{
public:
    typedef typename detail::ListNode <T, Tag> Node;

    typedef T                    value_type;
    typedef value_type*          pointer;
    typedef value_type&          reference;
    typedef value_type const*    const_pointer;
    typedef value_type const&    const_reference;
    typedef std::size_t          size_type;
    typedef std::ptrdiff_t       difference_type;

    typedef detail::ListIterator <Node>       iterator;
    typedef detail::ListIterator <Node const> const_iterator;

    /** Create an empty list. */
    List ()
    {
        m_head.m_prev = nullptr; // identifies the head
        m_tail.m_next = nullptr; // identifies the tail
        clear ();
    }

    /** Determine if the list is empty.
        @return `true` if the list is empty.
    */
    bool empty () const noexcept
    {
        return size () == 0;
    }

    /** Returns the number of elements in the list. */
    size_type size () const noexcept
    {
        return m_size;
    }

    /** Obtain a reference to the first element.
        @invariant The list may not be empty.
        @return A reference to the first element.
    */
    reference front () noexcept
    {
        return element_from (m_head.m_next);
    }

    /** Obtain a const reference to the first element.
        @invariant The list may not be empty.
        @return A const reference to the first element.
    */
    const_reference front () const noexcept
    {
        return element_from (m_head.m_next);
    }

    /** Obtain a reference to the last element.
        @invariant The list may not be empty.
        @return A reference to the last element.
    */
    reference back () noexcept
    {
        return element_from (m_tail.m_prev);
    }

    /** Obtain a const reference to the last element.
        @invariant The list may not be empty.
        @return A const reference to the last element.
    */
    const_reference back () const noexcept
    {
        return element_from (m_tail.m_prev);
    }

    /** Obtain an iterator to the beginning of the list.
        @return An iterator pointing to the beginning of the list.
    */
    iterator begin () noexcept
    {
        return iterator (m_head.m_next);
    }

    /** Obtain a const iterator to the beginning of the list.
        @return A const iterator pointing to the beginning of the list.
    */
    const_iterator begin () const noexcept
    {
        return const_iterator (m_head.m_next);
    }

    /** Obtain a const iterator to the beginning of the list.
        @return A const iterator pointing to the beginning of the list.
    */
    const_iterator cbegin () const noexcept
    {
        return const_iterator (m_head.m_next);
    }

    /** Obtain a iterator to the end of the list.
        @return An iterator pointing to the end of the list.
    */
    iterator end () noexcept
    {
        return iterator (&m_tail);
    }

    /** Obtain a const iterator to the end of the list.
        @return A constiterator pointing to the end of the list.
    */
    const_iterator end () const noexcept
    {
        return const_iterator (&m_tail);
    }

    /** Obtain a const iterator to the end of the list
        @return A constiterator pointing to the end of the list.
    */
    const_iterator cend () const noexcept
    {
        return const_iterator (&m_tail);
    }

    /** Clear the list.
        @note This does not free the elements.
    */
    void clear () noexcept
    {
        m_head.m_next = &m_tail;
        m_tail.m_prev = &m_head;
        m_size = 0;
    }

    /** Insert an element.
        @invariant The element must not already be in the list.
        @param pos The location to insert after.
        @param element The element to insert.
        @return An iterator pointing to the newly inserted element.
    */
    iterator insert (iterator pos, T& element) noexcept
    {
        Node* node = static_cast <Node*> (&element);
        node->m_next = &*pos;
        node->m_prev = node->m_next->m_prev;
        node->m_next->m_prev = node;
        node->m_prev->m_next = node;
        ++m_size;
        return iterator (node);
    }

    /** Insert another list into this one.
        The other list is cleared.
        @param pos The location to insert after.
        @param other The list to insert.
    */
    void insert (iterator pos, List& other) noexcept
    {
        if (!other.empty ())
        {
            Node* before = &*pos;
            other.m_head.m_next->m_prev = before->m_prev;
            before->m_prev->m_next = other.m_head.m_next;
            other.m_tail.m_prev->m_next = before;
            before->m_prev = other.m_tail.m_prev;
            m_size += other.m_size;
            other.clear ();
        }
    }

    /** Remove an element.
        @invariant The element must exist in the list.
        @param pos An iterator pointing to the element to remove.
        @return An iterator pointing to the next element after the one removed.
    */
    iterator erase (iterator pos) noexcept
    {
        Node* node = &*pos;
        ++pos;
        node->m_next->m_prev = node->m_prev;
        node->m_prev->m_next = node->m_next;
        --m_size;
        return pos;
    }

    /** Insert an element at the beginning of the list.
        @invariant The element must not exist in the list.
        @param element The element to insert.
    */
    iterator push_front (T& element) noexcept
    {
        return insert (begin (), element);
    }

    /** Remove the element at the beginning of the list.
        @invariant The list must not be empty.
        @return A reference to the popped element.
    */
    T& pop_front () noexcept
    {
        T& element (front ());
        erase (begin ());
        return element;
    }

    /** Append an element at the end of the list.
        @invariant The element must not exist in the list.
        @param element The element to append.
    */
    iterator push_back (T& element) noexcept
    {
        return insert (end (), element);
    }

    /** Remove the element at the end of the list.
        @invariant The list must not be empty.
        @return A reference to the popped element.
    */
    T& pop_back () noexcept
    {
        T& element (back ());
        erase (--end ());
        return element;
    }

    /** Swap contents with another list. */
    void swap (List& other) noexcept
    {
        List temp;
        temp.append (other);
        other.append (*this);
        append (temp);
    }

    /** Insert another list at the beginning of this list.
        The other list is cleared.
        @param list The other list to insert.
    */
    iterator prepend (List& list) noexcept
    {
        return insert (begin (), list);
    }

    /** Append another list at the end of this list.
        The other list is cleared.
        @param list the other list to append.
    */
    iterator append (List& list) noexcept
    {
        return insert (end (), list);
    }

    /** Obtain an iterator from an element.
        @invariant The element must exist in the list.
        @param element The element to obtain an iterator for.
        @return An iterator to the element.
    */
    iterator iterator_to (T& element) const noexcept
    {
        return iterator (static_cast <Node*> (&element));
    }

    /** Obtain a const iterator from an element.
        @invariant The element must exist in the list.
        @param element The element to obtain an iterator for.
        @return A const iterator to the element.
    */
    const_iterator const_iterator_to (T const& element) const noexcept
    {
        return const_iterator (static_cast <Node const*> (&element));
    }

private:
    reference element_from (Node* node) noexcept
    {
        return *(static_cast <pointer> (node));
    }

    const_reference element_from (Node const* node) const noexcept
    {
        return *(static_cast <const_pointer> (node));
    }

private:
    size_type m_size;
    Node m_head;
    Node m_tail;
};

}

#endif
