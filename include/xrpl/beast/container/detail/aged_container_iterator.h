#pragma once

#include <iterator>
#include <type_traits>
#include <utility>

namespace beast::detail {

// If Iterator is SCARY then this iterator will be as well.
template <bool IsConst, class Iterator>
class AgedContainerIterator
{
public:
    using IteratorCategory = std::iterator_traits<Iterator>::iterator_category;
    using ValueType = std::conditional_t<
        IsConst,
        typename Iterator::value_type::Stashed::ValueType const,
        typename Iterator::value_type::Stashed::ValueType>;
    using DifferenceType = std::iterator_traits<Iterator>::difference_type;
    using Pointer = ValueType*;
    using Reference = ValueType&;

    // Required by std::iterator_traits.
    // NOLINTBEGIN(readability-identifier-naming)
    using iterator_category = IteratorCategory;
    using value_type = ValueType;
    using difference_type = DifferenceType;
    // NOLINTEND(readability-identifier-naming)

    using TimePoint = Iterator::value_type::Stashed::TimePoint;

    AgedContainerIterator() = default;

    // Disable constructing a const_iterator from a non-const_iterator.
    // Converting between reverse and non-reverse iterators should be explicit.
    template <bool OtherIsConst, class OtherIterator>
    explicit AgedContainerIterator(AgedContainerIterator<OtherIsConst, OtherIterator> const& other)
        requires(
            (!OtherIsConst || IsConst) &&
            !static_cast<bool>(std::is_same_v<Iterator, OtherIterator>))
        : iter_(other.iter_)
    {
    }

    // Disable constructing a const_iterator from a non-const_iterator.
    template <bool OtherIsConst>
    AgedContainerIterator(AgedContainerIterator<OtherIsConst, Iterator> const& other)
        requires(!OtherIsConst || IsConst)
        : iter_(other.iter_)
    {
    }

    // Disable assigning a const_iterator to a non-const iterator
    template <bool OtherIsConst, class OtherIterator>
    auto
    operator=(AgedContainerIterator<OtherIsConst, OtherIterator> const& other)
        -> AgedContainerIterator&
        requires(!OtherIsConst || IsConst)
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

    Reference
    operator*() const
    {
        return iter_->value;
    }

    Pointer
    operator->() const
    {
        return &iter_->value;
    }

    [[nodiscard]] TimePoint const&
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

}  // namespace beast::detail
