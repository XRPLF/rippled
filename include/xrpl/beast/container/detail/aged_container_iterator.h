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
    using iterator_category = std::iterator_traits<Iterator>::iterator_category;
    using value_type = std::conditional_t<
        IsConst,
        typename Iterator::value_type::Stashed::value_type const,
        typename Iterator::value_type::Stashed::value_type>;
    using difference_type = std::iterator_traits<Iterator>::difference_type;
    using pointer = value_type*;
    using reference = value_type&;
    using time_point = Iterator::value_type::Stashed::time_point;

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
        : iter_(other.iter_)
    {
    }

    // Disable constructing a const_iterator from a non-const_iterator.
    template <bool OtherIsConst, class = std::enable_if_t<!OtherIsConst || IsConst>>
    AgedContainerIterator(AgedContainerIterator<OtherIsConst, Iterator> const& other)
        : iter_(other.iter_)
    {
    }

    // Disable assigning a const_iterator to a non-const iterator
    template <bool OtherIsConst, class OtherIterator>
    auto
    operator=(AgedContainerIterator<OtherIsConst, OtherIterator> const& other)
        -> std::enable_if_t<!OtherIsConst || IsConst, AgedContainerIterator&>
    {
        iter_ = other.iter_;
        return *this;
    }

    template <bool OtherIsConst, class OtherIterator>
    bool
    operator==(AgedContainerIterator<OtherIsConst, OtherIterator> const& other) const
    {
        return iter_ == other.iter_;
    }

    template <bool OtherIsConst, class OtherIterator>
    bool
    operator!=(AgedContainerIterator<OtherIsConst, OtherIterator> const& other) const
    {
        return iter_ != other.iter_;
    }

    AgedContainerIterator&
    operator++()
    {
        ++iter_;
        return *this;
    }

    AgedContainerIterator
    operator++(int)
    {
        AgedContainerIterator const prev(*this);
        ++iter_;
        return prev;
    }

    AgedContainerIterator&
    operator--()
    {
        --iter_;
        return *this;
    }

    AgedContainerIterator
    operator--(int)
    {
        AgedContainerIterator const prev(*this);
        --iter_;
        return prev;
    }

    reference
    operator*() const
    {
        return iter_->value;
    }

    pointer
    operator->() const
    {
        return &iter_->value;
    }

    [[nodiscard]] time_point const&
    when() const
    {
        return iter_->when;
    }

private:
    template <bool, bool, class, class, class, class, class>
    friend class AgedOrderedContainer;

    template <bool, bool, class, class, class, class, class, class>
    friend class AgedUnorderedContainer;

    template <bool, class>
    friend class AgedContainerIterator;

    template <class OtherIterator>
    AgedContainerIterator(OtherIterator iter) : iter_(std::move(iter))
    {
    }

    [[nodiscard]] Iterator const&
    iterator() const
    {
        return iter_;
    }

    Iterator iter_;
};

}  // namespace detail

}  // namespace beast
