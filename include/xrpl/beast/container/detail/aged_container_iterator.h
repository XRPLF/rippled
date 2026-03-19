#pragma once

#include <iterator>
#include <type_traits>

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
    using value_type = typename std::conditional<
        IsConst,
        typename Iterator::value_type::Stashed::value_type const,
        typename Iterator::value_type::Stashed::value_type>::type;
    using difference_type = typename std::iterator_traits<Iterator>::difference_type;
    using pointer = value_type*;
    using reference = value_type&;
    using time_point = typename Iterator::value_type::Stashed::time_point;

    AgedContainerIterator() = default;

    // Disable constructing a const_iterator from a non-const_iterator.
    // Converting between reverse and non-reverse iterators should be explicit.
    template <
        bool OtherIsConst,
        class OtherIterator,
        class = typename std::enable_if<
            (OtherIsConst == false || IsConst == true) &&
            std::is_same<Iterator, OtherIterator>::value == false>::type>
    explicit AgedContainerIterator(AgedContainerIterator<OtherIsConst, OtherIterator> const& other)
        : iter_(other.iter_)
    {
    }

    // Disable constructing a const_iterator from a non-const_iterator.
    template <
        bool OtherIsConst,
        class = typename std::enable_if<OtherIsConst == false || IsConst == true>::type>
    AgedContainerIterator(AgedContainerIterator<OtherIsConst, Iterator> const& other)
        : iter_(other.iter_)
    {
    }

    // Disable assigning a const_iterator to a non-const iterator
    template <bool OtherIsConst, class OtherIterator>
    auto
    operator=(AgedContainerIterator<OtherIsConst, OtherIterator> const& other) -> typename std::
        enable_if<OtherIsConst == false || IsConst == true, AgedContainerIterator&>::type
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

    time_point const&
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
    AgedContainerIterator(OtherIterator const& iter) : iter_(iter)
    {
    }

    Iterator const&
    iterator() const
    {
        return iter_;
    }

    Iterator iter_;
};

}  // namespace detail

}  // namespace beast
