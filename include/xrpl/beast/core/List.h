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
#include <type_traits>

namespace beast {

template <typename, typename>
class List;

namespace detail {

/** Copy `const` attribute from T to U if present. */
/** @{ */
template <typename T, typename U>
struct CopyConst
{
    explicit CopyConst() = default;

    using type = typename std::remove_const<U>::type;
};

template <typename T, typename U>
struct CopyConst<T const, U>
{
    explicit CopyConst() = default;

    using type = typename std::remove_const<U>::type const;
};
/** @} */

// This is the intrusive portion of the doubly linked list.
// One derivation per list that the object may appear on
// concurrently is required.
//
template <typename T, typename Tag>
class ListNode
{
private:
    using value_type = T;

    friend class List<T, Tag>;

    template <typename>
    friend class ListIterator;

    ListNode* m_next;
    ListNode* m_prev;
};

//------------------------------------------------------------------------------

template <typename N>
class ListIterator
{
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type =
        typename beast::detail::CopyConst<N, typename N::value_type>::type;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;
    using size_type = std::size_t;

    ListIterator(N* node = nullptr) noexcept : m_node(node)
    {
    }

    template <typename M>
    ListIterator(ListIterator<M> const& other) noexcept : m_node(other.m_node)
    {
    }

    template <typename M>
    bool
    operator==(ListIterator<M> const& other) const noexcept
    {
        return m_node == other.m_node;
    }

    template <typename M>
    bool
    operator!=(ListIterator<M> const& other) const noexcept
    {
        return !((*this) == other);
    }

    reference
    operator*() const noexcept
    {
        return dereference();
    }

    pointer
    operator->() const noexcept
    {
        return &dereference();
    }

    ListIterator&
    operator++() noexcept
    {
        increment();
        return *this;
    }

    ListIterator
    operator++(int) noexcept
    {
        ListIterator result(*this);
        increment();
        return result;
    }

    ListIterator&
    operator--() noexcept
    {
        decrement();
        return *this;
    }

    ListIterator
    operator--(int) noexcept
    {
        ListIterator result(*this);
        decrement();
        return result;
    }

private:
    reference
    dereference() const noexcept
    {
        return static_cast<reference>(*m_node);
    }

    void
    increment() noexcept
    {
        m_node = m_node->m_next;
    }

    void
    decrement() noexcept
    {
        m_node = m_node->m_prev;
    }

    N* m_node;
};

}  // namespace detail

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

    using ProcessList = List <Actor, ProcessTag>;
    using UpdateList = List <Actor, UpdateTag>;

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

    @tparam Tag An optional unique type name used to distinguish lists and
   nodes, when the object can exist in multiple lists simultaneously.

    @ingroup beast_core intrusive
*/
template <typename T, typename Tag = void>
class List
{
public:
    using Node = typename detail::ListNode<T, Tag>;

    using value_type = T;
    using pointer = value_type*;
    using reference = value_type&;
    using const_pointer = value_type const*;
    using const_reference = value_type const&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    using iterator = detail::ListIterator<Node>;
    using const_iterator = detail::ListIterator<Node const>;

    /** Create an empty list. */
    List()
    {
        m_head.m_prev = nullptr;  // identifies the head
        m_tail.m_next = nullptr;  // identifies the tail
        clear();
    }

    List(List const&) = delete;
    List&
    operator=(List const&) = delete;

    /** Determine if the list is empty.
        @return `true` if the list is empty.
    */
    bool
    empty() const noexcept
    {
        return size() == 0;
    }

    /** Returns the number of elements in the list. */
    size_type
    size() const noexcept
    {
        return m_size;
    }

    /** Obtain a reference to the first element.
        @invariant The list may not be empty.
        @return A reference to the first element.
    */
    reference
    front() noexcept
    {
        return element_from(m_head.m_next);
    }

    /** Obtain a const reference to the first element.
        @invariant The list may not be empty.
        @return A const reference to the first element.
    */
    const_reference
    front() const noexcept
    {
        return element_from(m_head.m_next);
    }

    /** Obtain a reference to the last element.
        @invariant The list may not be empty.
        @return A reference to the last element.
    */
    reference
    back() noexcept
    {
        return element_from(m_tail.m_prev);
    }

    /** Obtain a const reference to the last element.
        @invariant The list may not be empty.
        @return A const reference to the last element.
    */
    const_reference
    back() const noexcept
    {
        return element_from(m_tail.m_prev);
    }

    /** Obtain an iterator to the beginning of the list.
        @return An iterator pointing to the beginning of the list.
    */
    iterator
    begin() noexcept
    {
        return iterator(m_head.m_next);
    }

    /** Obtain a const iterator to the beginning of the list.
        @return A const iterator pointing to the beginning of the list.
    */
    const_iterator
    begin() const noexcept
    {
        return const_iterator(m_head.m_next);
    }

    /** Obtain a const iterator to the beginning of the list.
        @return A const iterator pointing to the beginning of the list.
    */
    const_iterator
    cbegin() const noexcept
    {
        return const_iterator(m_head.m_next);
    }

    /** Obtain a iterator to the end of the list.
        @return An iterator pointing to the end of the list.
    */
    iterator
    end() noexcept
    {
        return iterator(&m_tail);
    }

    /** Obtain a const iterator to the end of the list.
        @return A constiterator pointing to the end of the list.
    */
    const_iterator
    end() const noexcept
    {
        return const_iterator(&m_tail);
    }

    /** Obtain a const iterator to the end of the list
        @return A constiterator pointing to the end of the list.
    */
    const_iterator
    cend() const noexcept
    {
        return const_iterator(&m_tail);
    }

    /** Clear the list.
        @note This does not free the elements.
    */
    void
    clear() noexcept
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
    iterator
    insert(iterator pos, T& element) noexcept
    {
        Node* node = static_cast<Node*>(&element);
        node->m_next = &*pos;
        node->m_prev = node->m_next->m_prev;
        node->m_next->m_prev = node;
        node->m_prev->m_next = node;
        ++m_size;
        return iterator(node);
    }

    /** Insert another list into this one.
        The other list is cleared.
        @param pos The location to insert after.
        @param other The list to insert.
    */
    void
    insert(iterator pos, List& other) noexcept
    {
        if (!other.empty())
        {
            Node* before = &*pos;
            other.m_head.m_next->m_prev = before->m_prev;
            before->m_prev->m_next = other.m_head.m_next;
            other.m_tail.m_prev->m_next = before;
            before->m_prev = other.m_tail.m_prev;
            m_size += other.m_size;
            other.clear();
        }
    }

    /** Remove an element.
        @invariant The element must exist in the list.
        @param pos An iterator pointing to the element to remove.
        @return An iterator pointing to the next element after the one removed.
    */
    iterator
    erase(iterator pos) noexcept
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
    iterator
    push_front(T& element) noexcept
    {
        return insert(begin(), element);
    }

    /** Remove the element at the beginning of the list.
        @invariant The list must not be empty.
        @return A reference to the popped element.
    */
    T&
    pop_front() noexcept
    {
        T& element(front());
        erase(begin());
        return element;
    }

    /** Append an element at the end of the list.
        @invariant The element must not exist in the list.
        @param element The element to append.
    */
    iterator
    push_back(T& element) noexcept
    {
        return insert(end(), element);
    }

    /** Remove the element at the end of the list.
        @invariant The list must not be empty.
        @return A reference to the popped element.
    */
    T&
    pop_back() noexcept
    {
        T& element(back());
        erase(--end());
        return element;
    }

    /** Swap contents with another list. */
    void
    swap(List& other) noexcept
    {
        List temp;
        temp.append(other);
        other.append(*this);
        append(temp);
    }

    /** Insert another list at the beginning of this list.
        The other list is cleared.
        @param list The other list to insert.
    */
    iterator
    prepend(List& list) noexcept
    {
        return insert(begin(), list);
    }

    /** Append another list at the end of this list.
        The other list is cleared.
        @param list the other list to append.
    */
    iterator
    append(List& list) noexcept
    {
        return insert(end(), list);
    }

    /** Obtain an iterator from an element.
        @invariant The element must exist in the list.
        @param element The element to obtain an iterator for.
        @return An iterator to the element.
    */
    iterator
    iterator_to(T& element) const noexcept
    {
        return iterator(static_cast<Node*>(&element));
    }

    /** Obtain a const iterator from an element.
        @invariant The element must exist in the list.
        @param element The element to obtain an iterator for.
        @return A const iterator to the element.
    */
    const_iterator
    const_iterator_to(T const& element) const noexcept
    {
        return const_iterator(static_cast<Node const*>(&element));
    }

private:
    reference
    element_from(Node* node) noexcept
    {
        return *(static_cast<pointer>(node));
    }

    const_reference
    element_from(Node const* node) const noexcept
    {
        return *(static_cast<const_pointer>(node));
    }

private:
    size_type m_size;
    Node m_head;
    Node m_tail;
};

}  // namespace beast

#endif
