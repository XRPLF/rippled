#pragma once

#include <atomic>
#include <iterator>
#include <type_traits>

namespace beast {

//------------------------------------------------------------------------------

template <class Container, bool IsConst>
class LockFreeStackIterator
{
protected:
    using Node = Container::Node;
    using NodePtr = std::conditional_t<IsConst, Node const*, Node*>;

public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Container::value_type;
    using difference_type = Container::difference_type;
    using pointer =
        std::conditional_t<IsConst, typename Container::const_pointer, typename Container::pointer>;
    using reference = std::
        conditional_t<IsConst, typename Container::const_reference, typename Container::reference>;

    LockFreeStackIterator() = default;

    LockFreeStackIterator(NodePtr node) : node_(node)
    {
    }

    template <bool OtherIsConst>
    explicit LockFreeStackIterator(LockFreeStackIterator<Container, OtherIsConst> const& other)
        : node_(other.node_)
    {
    }

    LockFreeStackIterator&
    operator=(NodePtr node)
    {
        node_ = node;
        return static_cast<LockFreeStackIterator&>(*this);
    }

    LockFreeStackIterator&
    operator++()
    {
        node_ = node_->next_.load();
        return static_cast<LockFreeStackIterator&>(*this);
    }

    LockFreeStackIterator
    operator++(int)
    {
        LockFreeStackIterator result(*this);
        node_ = node_->next_;
        return result;
    }

    NodePtr
    node() const
    {
        return node_;
    }

    reference
    operator*() const
    {
        return *this->operator->();
    }

    pointer
    operator->() const
    {
        return static_cast<pointer>(node_);
    }

private:
    NodePtr node_{};
};

//------------------------------------------------------------------------------

template <class Container, bool LhsIsConst, bool RhsIsConst>
bool
operator==(
    LockFreeStackIterator<Container, LhsIsConst> const& lhs,
    LockFreeStackIterator<Container, RhsIsConst> const& rhs)
{
    return lhs.node() == rhs.node();
}

template <class Container, bool LhsIsConst, bool RhsIsConst>
bool
operator!=(
    LockFreeStackIterator<Container, LhsIsConst> const& lhs,
    LockFreeStackIterator<Container, RhsIsConst> const& rhs)
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
class LockFreeStack
{
public:
    class Node
    {
    public:
        Node() : next_(nullptr)
        {
        }

        explicit Node(Node* next) : next_(next)
        {
        }

        Node(Node const&) = delete;
        Node&
        operator=(Node const&) = delete;

    private:
        friend class LockFreeStack;

        template <class Container, bool IsConst>
        friend class LockFreeStackIterator;

        std::atomic<Node*> next_;
    };

public:
    using value_type = Element;
    using pointer = Element*;
    using reference = Element&;
    using const_pointer = Element const*;
    using const_reference = Element const&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using iterator = LockFreeStackIterator<LockFreeStack<Element, Tag>, false>;
    using const_iterator = LockFreeStackIterator<LockFreeStack<Element, Tag>, true>;

    LockFreeStack() : end_(nullptr), head_(&end_)
    {
    }

    LockFreeStack(LockFreeStack const&) = delete;
    LockFreeStack&
    operator=(LockFreeStack const&) = delete;

    /** Returns true if the stack is empty. */
    [[nodiscard]] bool
    empty() const
    {
        return head_.load() == &end_;
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
    bool
    pushFront(Node* node)
    {
        bool first = false;
        Node* oldHead = head_.load(std::memory_order_relaxed);
        do
        {
            first = (oldHead == &end_);
            node->next_ = oldHead;
        } while (!head_.compare_exchange_strong(
            oldHead, node, std::memory_order_release, std::memory_order_relaxed));
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
    Element*
    popFront()
    {
        Node* node = head_.load();
        Node* newHead = nullptr;
        do
        {
            if (node == &end_)
                return nullptr;
            newHead = node->next_.load();
        } while (!head_.compare_exchange_strong(
            node, newHead, std::memory_order_release, std::memory_order_relaxed));
        return static_cast<Element*>(node);
    }

    /** Return a forward iterator to the beginning or end of the stack.
        Undefined behavior results if push_front or pop_front is called
        while an iteration is in progress.
        Thread safety:
            Caller is responsible for synchronization.
    */
    /** @{ */
    iterator
    begin()
    {
        return iterator(head_.load());
    }

    iterator
    end()
    {
        return iterator(&end_);
    }

    [[nodiscard]] const_iterator
    begin() const
    {
        return const_iterator(head_.load());
    }

    [[nodiscard]] const_iterator
    end() const
    {
        return const_iterator(&end_);
    }

    [[nodiscard]] const_iterator
    cbegin() const
    {
        return const_iterator(head_.load());
    }

    [[nodiscard]] const_iterator
    cend() const
    {
        return const_iterator(&end_);
    }
    /** @} */

private:
    Node end_;
    std::atomic<Node*> head_;
};

}  // namespace beast
