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
    using Node = typename Container::Node;
    using NodePtr = std::conditional_t<IsConst, Node const*, Node*>;

public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = typename Container::value_type;
    using difference_type = typename Container::difference_type;
    using pointer =
        std::conditional_t<IsConst, typename Container::const_pointer, typename Container::pointer>;
    using reference = std::
        conditional_t<IsConst, typename Container::const_reference, typename Container::reference>;

    LockFreeStackIterator() = default;

    LockFreeStackIterator(NodePtr node) : m_node_(node)
    {
    }

    template <bool OtherIsConst>
    explicit LockFreeStackIterator(LockFreeStackIterator<Container, OtherIsConst> const& other)
        : m_node_(other.m_node)
    {
    }

    LockFreeStackIterator&
    operator=(NodePtr node)
    {
        m_node_ = node;
        return static_cast<LockFreeStackIterator&>(*this);
    }

    LockFreeStackIterator&
    operator++()
    {
        m_node_ = m_node_->m_next.load();
        return static_cast<LockFreeStackIterator&>(*this);
    }

    LockFreeStackIterator
    operator++(int)
    {
        LockFreeStackIterator result(*this);
        m_node_ = m_node_->m_next;
        return result;
    }

    NodePtr
    node() const
    {
        return m_node_;
    }

    reference
    operator*() const
    {
        return *this->operator->();
    }

    pointer
    operator->() const
    {
        return static_cast<pointer>(m_node_);
    }

private:
    NodePtr m_node_{};
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
        Node() : m_next_(nullptr)
        {
        }

        explicit Node(Node* next) : m_next_(next)
        {
        }

        Node(Node const&) = delete;
        Node&
        operator=(Node const&) = delete;

    private:
        friend class LockFreeStack;

        template <class Container, bool IsConst>
        friend class LockFreeStackIterator;

        std::atomic<Node*> m_next_;
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

    LockFreeStack() : m_end_(nullptr), m_head_(&m_end_)
    {
    }

    LockFreeStack(LockFreeStack const&) = delete;
    LockFreeStack&
    operator=(LockFreeStack const&) = delete;

    /** Returns true if the stack is empty. */
    [[nodiscard]] bool
    empty() const
    {
        return m_head_.load() == &m_end_;
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
        Node* oldHead = m_head_.load(std::memory_order_relaxed);
        do
        {
            first = (oldHead == &m_end_);
            node->m_next_ = oldHead;
        } while (!m_head_.compare_exchange_strong(
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
        Node* node = m_head_.load();
        Node* newHead = nullptr;
        do
        {
            if (node == &m_end_)
                return nullptr;
            newHead = node->m_next_.load();
        } while (!m_head_.compare_exchange_strong(
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
        return iterator(m_head_.load());
    }

    iterator
    end()
    {
        return iterator(&m_end_);
    }

    [[nodiscard]] const_iterator
    begin() const
    {
        return const_iterator(m_head_.load());
    }

    [[nodiscard]] const_iterator
    end() const
    {
        return const_iterator(&m_end_);
    }

    [[nodiscard]] const_iterator
    cbegin() const
    {
        return const_iterator(m_head_.load());
    }

    [[nodiscard]] const_iterator
    cend() const
    {
        return const_iterator(&m_end_);
    }
    /** @} */

private:
    Node m_end_;
    std::atomic<Node*> m_head_;
};

}  // namespace beast
