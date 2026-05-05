#pragma once

#include <iterator>
#include <type_traits>
#include <utility>

namespace beast {

template <bool, bool, class, class, class, class, class>
class aged_ordered_container;

namespace detail {

// If Iterator is SCARY then this iterator will be as well.
template <bool IsConst, class Iterator>
class AgedContainerIterator
{
public:
    using iterator_category = typename std::iterator_traits<Iterator>::iterator_category;
    using value_type = std::conditional_t<
        IsConst,
        typename Iterator::value_type::stashed::value_type const,
        typename Iterator::value_type::stashed::value_type>;
    using difference_type = typename std::iterator_traits<Iterator>::difference_type;
    using pointer = value_type*;
    using reference = value_type&;
    using time_point = typename Iterator::value_type::stashed::time_point;

    AgedContainerIterator() = default;

    // Disable constructing a const_iterator from a non-const_iterator.
    // Converting between reverse and non-reverse iterators should be explicit.
    template <
        bool OtherIsConst,
        class OtherIterator,
        class = std::enable_if_t<
            (!OtherIsConst || IsConst) &&
            !static_cast<bool>(std::is_same_v<Iterator, OtherIterator>)>>
    explicit AgedContainerIterator(AgedContainerIterator<OtherIsConst, OtherIterator> const& other)
        : m_iter_(other.m_iter)
    {
    }

    // Disable constructing a const_iterator from a non-const_iterator.
    template <bool OtherIsConst, class = std::enable_if_t<!OtherIsConst || IsConst>>
    AgedContainerIterator(AgedContainerIterator<OtherIsConst, Iterator> const& other)
        : m_iter_(other.m_iter)
    {
    }

    // Disable assigning a const_iterator to a non-const iterator
    template <bool OtherIsConst, class OtherIterator>
    auto
    operator=(AgedContainerIterator<OtherIsConst, OtherIterator> const& other)
        -> std::enable_if_t<!OtherIsConst || IsConst, AgedContainerIterator&>
    {
        m_iter_ = other.m_iter;
        return *this;
    }

    template <bool OtherIsConst, class OtherIterator>
    bool
    operator==(AgedContainerIterator<OtherIsConst, OtherIterator> const& other) const
    {
        return m_iter_ == other.m_iter;
    }

    template <bool OtherIsConst, class OtherIterator>
    bool
    operator!=(AgedContainerIterator<OtherIsConst, OtherIterator> const& other) const
    {
        return m_iter_ != other.m_iter;
    }

    AgedContainerIterator&
    operator++()
    {
        ++m_iter_;
        return *this;
    }

    AgedContainerIterator
    operator++(int)
    {
        AgedContainerIterator const prev(*this);
        ++m_iter_;
        return prev;
    }

    AgedContainerIterator&
    operator--()
    {
        --m_iter_;
        return *this;
    }

    AgedContainerIterator
    operator--(int)
    {
        AgedContainerIterator const prev(*this);
        --m_iter_;
        return prev;
    }

    reference
    operator*() const
    {
        return m_iter_->value;
    }

    pointer
    operator->() const
    {
        return &m_iter_->value;
    }

    [[nodiscard]] time_point const&
    when() const
    {
        return m_iter_->when;
    }

private:
    template <bool, bool, class, class, class, class, class>
    friend class aged_ordered_container;

    template <bool, bool, class, class, class, class, class, class>
    friend class aged_unordered_container;

    template <bool, class>
    friend class aged_container_iterator;

    template <class OtherIterator>
    AgedContainerIterator(OtherIterator iter) : m_iter_(std::move(iter))
    {
    }

    [[nodiscard]] Iterator const&
    iterator() const
    {
        return m_iter_;
    }

    Iterator m_iter_;
};

}  // namespace detail

}  // namespace beast
