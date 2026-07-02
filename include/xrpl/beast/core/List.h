#pragma once

#include <iterator>

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

    using type = std::remove_const_t<U>;
};

template <typename T, typename U>
struct CopyConst<T const, U>
{
    explicit CopyConst() = default;

    using type = std::remove_const<U>::type const;
};
/** @} */

// This is the intrusive portion of the doubly linked list.
// One derivation per list that the object may appear on
// concurrently is required.
//
template <typename T, typename Tag>
class ListNode
{
    ListNode() = default;

    using value_type = T;

    friend T;
    friend class List<T, Tag>;

    template <typename>
    friend class ListIterator;

    ListNode* next_ = nullptr;
    ListNode* prev_ = nullptr;
};

//------------------------------------------------------------------------------

template <typename N>
class ListIterator
{
public:
    using iterator_category = std::bidirectional_iterator_tag;
    using value_type = beast::detail::CopyConst<N, typename N::value_type>::type;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;
    using size_type = std::size_t;

    ListIterator(N* node = nullptr) noexcept : node_(node)
    {
    }

    template <typename M>
    ListIterator(ListIterator<M> const& other) noexcept : node_(other.node_)
    {
    }

    template <typename M>
    bool
    operator==(ListIterator<M> const& other) const noexcept
    {
        return node_ == other.node_;
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
    [[nodiscard]] reference
    dereference() const noexcept
    {
        return static_cast<reference>(*node_);
    }

    void
    increment() noexcept
    {
        node_ = node_->next_;
    }

    void
    decrement() noexcept
    {
        node_ = node_->prev_;
    }

    N* node_;
};

}  // namespace detail

/** Intrusive doubly linked list.

    This intrusive List is a container similar in operation to std::list in the
    Standard Template Library (STL). Like all @ref intrusive containers, List
    requires you to first derive your class from List<>::Node:

    @code

    struct Object : List <Object>::Node
    {
        explicit Object (int value) : value_ (value)
        {
        }

        int value_;
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
        std::cout << iter->value_;

    @endcode

    You can even use BOOST_FOREACH, or range based for loops:

    @code

    BOOST_FOREACH (Object& object, list)  // boost only
        std::cout << object.value_;

    for (Object& object : list)           // C++11 only
        std::cout << object.value_;

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
    using Node = detail::ListNode<T, Tag>;

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
        head_.prev_ = nullptr;  // identifies the head
        tail_.next_ = nullptr;  // identifies the tail
        clear();
    }

    List(List const&) = delete;
    List&
    operator=(List const&) = delete;

    /** Determine if the list is empty.
        @return `true` if the list is empty.
    */
    [[nodiscard]] bool
    empty() const noexcept
    {
        return size() == 0;
    }

    /** Returns the number of elements in the list. */
    [[nodiscard]] size_type
    size() const noexcept
    {
        return size_;
    }

    /** Obtain a reference to the first element.
        @invariant The list may not be empty.
        @return A reference to the first element.
    */
    reference
    front() noexcept
    {
        return element_from(head_.next_);
    }

    /** Obtain a const reference to the first element.
        @invariant The list may not be empty.
        @return A const reference to the first element.
    */
    [[nodiscard]] const_reference
    front() const noexcept
    {
        return element_from(head_.next_);
    }

    /** Obtain a reference to the last element.
        @invariant The list may not be empty.
        @return A reference to the last element.
    */
    reference
    back() noexcept
    {
        return element_from(tail_.prev_);
    }

    /** Obtain a const reference to the last element.
        @invariant The list may not be empty.
        @return A const reference to the last element.
    */
    [[nodiscard]] const_reference
    back() const noexcept
    {
        return element_from(tail_.prev_);
    }

    /** Obtain an iterator to the beginning of the list.
        @return An iterator pointing to the beginning of the list.
    */
    iterator
    begin() noexcept
    {
        return iterator(head_.next_);
    }

    /** Obtain a const iterator to the beginning of the list.
        @return A const iterator pointing to the beginning of the list.
    */
    [[nodiscard]] const_iterator
    begin() const noexcept
    {
        return const_iterator(head_.next_);
    }

    /** Obtain a const iterator to the beginning of the list.
        @return A const iterator pointing to the beginning of the list.
    */
    [[nodiscard]] const_iterator
    cbegin() const noexcept
    {
        return const_iterator(head_.next_);
    }

    /** Obtain a iterator to the end of the list.
        @return An iterator pointing to the end of the list.
    */
    iterator
    end() noexcept
    {
        return iterator(&tail_);
    }

    /** Obtain a const iterator to the end of the list.
        @return A constiterator pointing to the end of the list.
    */
    [[nodiscard]] const_iterator
    end() const noexcept
    {
        return const_iterator(&tail_);
    }

    /** Obtain a const iterator to the end of the list
        @return A constiterator pointing to the end of the list.
    */
    [[nodiscard]] const_iterator
    cend() const noexcept
    {
        return const_iterator(&tail_);
    }

    /** Clear the list.
        @note This does not free the elements.
    */
    void
    clear() noexcept
    {
        head_.next_ = &tail_;
        tail_.prev_ = &head_;
        size_ = 0;
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
        node->next_ = &*pos;
        node->prev_ = node->next_->prev_;
        node->next_->prev_ = node;
        node->prev_->next_ = node;
        ++size_;
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
            other.head_.next_->prev_ = before->prev_;
            before->prev_->next_ = other.head_.next_;
            other.tail_.prev_->next_ = before;
            before->prev_ = other.tail_.prev_;
            size_ += other.size_;
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
        Node const* node = &*pos;
        ++pos;
        node->next_->prev_ = node->prev_;
        node->prev_->next_ = node->next_;
        --size_;
        return pos;
    }

    /** Insert an element at the beginning of the list.
        @invariant The element must not exist in the list.
        @param element The element to insert.
    */
    iterator
    pushFront(T& element) noexcept
    {
        return insert(begin(), element);
    }

    /** Remove the element at the beginning of the list.
        @invariant The list must not be empty.
        @return A reference to the popped element.
    */
    T&
    popFront() noexcept
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
    pushBack(T& element) noexcept
    {
        return insert(end(), element);
    }

    /** Remove the element at the end of the list.
        @invariant The list must not be empty.
        @return A reference to the popped element.
    */
    T&
    popBack() noexcept
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
    iteratorTo(T& element) const noexcept
    {
        return iterator(static_cast<Node*>(&element));
    }

    /** Obtain a const iterator from an element.
        @invariant The element must exist in the list.
        @param element The element to obtain an iterator for.
        @return A const iterator to the element.
    */
    [[nodiscard]] const_iterator
    constIteratorTo(T const& element) const noexcept
    {
        return const_iterator(static_cast<Node const*>(&element));
    }

private:
    reference
    elementFrom(Node* node) noexcept
    {
        return *(static_cast<pointer>(node));
    }

    const_reference
    elementFrom(Node const* node) const noexcept
    {
        return *(static_cast<const_pointer>(node));
    }

private:
    size_type size_ = 0u;
    Node head_;
    Node tail_;
};

}  // namespace beast
