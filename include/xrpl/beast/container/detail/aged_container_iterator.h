#pragma once

#include <iterator>
#include <type_traits>

namespace beast {

template <bool, bool, class, class, class, class, class>
class aged_ordered_container;

namespace detail {

// If Iterator is SCARY then this iterator will be as well.
template <bool is_const, class Iterator>
class aged_container_iterator
{
public:
    using iterator_category = typename std::iterator_traits<Iterator>::iterator_category;
    using value_type = typename std::conditional<
        is_const,
        typename Iterator::value_type::stashed::value_type const,
        typename Iterator::value_type::stashed::value_type>::type;
    using difference_type = typename std::iterator_traits<Iterator>::difference_type;
    using pointer = value_type*;
    using reference = value_type&;
    using time_point = typename Iterator::value_type::stashed::time_point;

    aged_container_iterator() = default;

    // Disable constructing a const_iterator from a non-const_iterator.
    // Converting between reverse and non-reverse iterators should be explicit.
    template <
        bool other_is_const,
        class OtherIterator,
        class = typename std::enable_if<
            (other_is_const == false || is_const == true) &&
            std::is_same<Iterator, OtherIterator>::value == false>::type>
    explicit aged_container_iterator(
        aged_container_iterator<other_is_const, OtherIterator> const& other)
        : iter_(other.iter_)
    {
    }

    // Disable constructing a const_iterator from a non-const_iterator.
    template <
        bool other_is_const,
        class = typename std::enable_if<other_is_const == false || is_const == true>::type>
    aged_container_iterator(aged_container_iterator<other_is_const, Iterator> const& other)
        : iter_(other.iter_)
    {
    }

    // Disable assigning a const_iterator to a non-const iterator
    template <bool other_is_const, class OtherIterator>
    auto
    operator=(aged_container_iterator<other_is_const, OtherIterator> const& other) -> typename std::
        enable_if<other_is_const == false || is_const == true, aged_container_iterator&>::type
    {
        iter_ = other.iter_;
        return *this;
    }

    template <bool other_is_const, class OtherIterator>
    bool
    operator==(aged_container_iterator<other_is_const, OtherIterator> const& other) const
    {
        return iter_ == other.iter_;
    }

    template <bool other_is_const, class OtherIterator>
    bool
    operator!=(aged_container_iterator<other_is_const, OtherIterator> const& other) const
    {
        return iter_ != other.iter_;
    }

    aged_container_iterator&
    operator++()
    {
        ++iter_;
        return *this;
    }

    aged_container_iterator
    operator++(int)
    {
        aged_container_iterator const prev(*this);
        ++iter_;
        return prev;
    }

    aged_container_iterator&
    operator--()
    {
        --iter_;
        return *this;
    }

    aged_container_iterator
    operator--(int)
    {
        aged_container_iterator const prev(*this);
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
    friend class aged_ordered_container;

    template <bool, bool, class, class, class, class, class, class>
    friend class aged_unordered_container;

    template <bool, class>
    friend class aged_container_iterator;

    template <class OtherIterator>
    aged_container_iterator(OtherIterator const& iter) : iter_(iter)
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
