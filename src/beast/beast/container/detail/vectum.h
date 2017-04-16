//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_CONTAINER_VECTUM_H_INCLUDED
#define BEAST_CONTAINER_VECTUM_H_INCLUDED

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <beast/cxx14/type_traits.h> // <type_traits>
#include <utility>

namespace beast {
namespace detail {

template <class>
class vectum;

template <class T>
void
swap (vectum<T>&, vectum<T>&);

// Custom vector where the element size is controlled
// at run-time during construction and can be larger than
// the value_type.
//
template <class T>
class vectum
{
private:
    template <bool is_const>
    class basic_iterator;
    
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
    std::size_t value_size_;
    std::size_t aligned_size_;
    std::unique_ptr<std::uint8_t[]> buf_;

public:
    using value_type = T;
    //using allocator_type = Alloc;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using const_reference = value_type const&;
    using pointer = value_type*;
    using const_pointer = value_type const*;
    using iterator = basic_iterator<false>;
    using const_iterator = basic_iterator<true>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    // Non-trivial construction is allowed because we
    // don't support vector operations like resize().
#ifdef _MSC_VER
    static_assert(
        //std::is_trivially_destructible<T>::value &&
        std::is_trivially_copy_constructible<value_type>::value &&
        std::is_trivially_copy_assignable<value_type>::value &&
        std::is_trivially_move_constructible<value_type>::value &&
        std::is_trivially_move_assignable<value_type>::value,
            "only trivial types supported");
#else
    // glibc++ doesn't seem to handle this
#endif

    vectum (vectum const&) = delete;
    vectum& operator= (vectum const&) = delete;

    explicit
    vectum (size_type value_size = sizeof(value_type));

    vectum& operator= (vectum&& other);

    reference
    at (size_type n)
    {
        if (n >= size_)
            throw std::out_of_range(
                "bad array index");
        return *element(n);
    }

    const_reference
    at (size_type n) const
    {
        if (n >= size_)
            throw std::out_of_range(
                "bad array index");
        return *element(n);
    }

    reference
    operator[] (std::size_t n)
    {
        return *element(n);
    }
    
    const_reference
    operator[] (std::size_t n) const
    {
        return *element(n);
    }

    reference
    front()
    {
        return *element(0);
    }

    const_reference
    front() const
    {
        return *element(0);
    }

    reference
    back()
    {
        return *element(size_-1);
    }

    const_reference
    back() const
    {
        return *element(size_-1);
    }

    iterator
    begin()
    {
        return iterator(
            element(0), aligned_size_);
    }

    const_iterator
    begin() const
    {
        return const_iterator(
            element(0), aligned_size_);
    }

    const_iterator
    cbegin() const
    {
        return const_iterator(
            element(0), aligned_size_);
    }

    iterator
    end()
    {
        return iterator(
            element(size_), aligned_size_);
    }

    const_iterator
    end() const
    {
        return const_iterator(
            element(size_), aligned_size_);
    }

    const_iterator
    cend() const
    {
        return const_iterator(
            element(size_), aligned_size_);
    }

    reverse_iterator
    rbegin()
    {
        return reverse_iterator(end());
    }

    const_reverse_iterator
    rbegin() const
    {
        return const_reverse_iterator(end());
    }

    const_reverse_iterator
    crbegin() const
    {
        return const_reverse_iterator(end());
    }

    reverse_iterator
    rend()
    {
        return reverse_iterator(begin());
    }

    const_reverse_iterator
    rend() const
    {
        return const_reverse_iterator(begin());
    }

    const_reverse_iterator
    crend() const
    {
        return const_reverse_iterator(begin());
    }

    bool
    empty() const
    {
        return size_ == 0;
    }

    size_type
    size() const
    {
        return size_;
    }

    size_type
    capacity() const
    {
        return capacity_;
    }

    void
    reserve (size_type new_cap);

    void
    shrink_to_fit();

    void
    clear();

    template <bool is_const, class... Args>
    iterator
    emplace (basic_iterator<is_const> const& pos,
        Args&&... args);

    template <class U>
    friend
    void
    swap (vectum<U>& lhs, vectum<U>& rhs);

private:
    pointer
    element (size_type n)
    {
        return reinterpret_cast<pointer>(
            (buf_.get() + n * aligned_size_));
    }

    const_pointer
    element (size_type n) const
    {
        return reinterpret_cast<const_pointer>(
            (buf_.get() + n * aligned_size_));
    }

    template <bool is_const>
    size_type
    indexof (basic_iterator<is_const> const& pos)
    {
        return (reinterpret_cast<uint8_t const*>(&*pos) -
            buf_.get()) / aligned_size_;
    }

    void
    destroy (std::false_type)
    {
        for (std::size_t i = 0; i < size_; ++i)
            element(i)->~value_type();
    }

    void
    destroy (std::true_type)
    {
    }

    void
    reset();
};

//------------------------------------------------------------------------------

template <class T>
template <bool is_const>
class vectum<T>::basic_iterator
{
private:
    using ptr_t = std::conditional_t <
        is_const, T const*, T*>;
    using byte_t = std::conditional_t <
        is_const, std::uint8_t const*, std::uint8_t*>;

    ptr_t ptr_ = nullptr;
    std::size_t aligned_size_;

public:
    using value_type =
        std::conditional_t <is_const, T const, T>;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;
    using iterator_category =
        std::random_access_iterator_tag;

    basic_iterator() = default;
    basic_iterator (basic_iterator const&) = default;
    basic_iterator& operator= (basic_iterator const&) = default;

    template <bool maybe_const,
        class = std::enable_if_t <is_const && ! maybe_const>>
    basic_iterator (basic_iterator<maybe_const> const& iter)
        : ptr_ (iter.ptr_)
        , aligned_size_ (iter.aligned_size_)
    {
    }

    reference
    operator*() const
    {
        return *ptr_;
    }

    pointer
    operator->() const
    {
        return ptr_;
    }

    reference&
    operator[] (std::ptrdiff_t n) const
    {
        return *increment(n);
    }

    template <bool maybe_const>
    bool
    operator== (basic_iterator<maybe_const> const& rhs) const
    {
        return ptr_ == rhs.ptr_;
    }

    template <bool maybe_const>
    bool
    operator!= (basic_iterator<
        maybe_const> const& rhs) const
    {
        return ptr_ != rhs.ptr_;
    }

    template <bool maybe_const>
    bool
    operator< (basic_iterator<
        maybe_const> const& rhs) const
    {
        return ptr_ < rhs.ptr_;
    }

    template <bool maybe_const>
    bool
    operator> (basic_iterator<
        maybe_const> const& rhs) const
    {
        return ptr_ > rhs.ptr_;
    }

    template <bool maybe_const>
    bool
    operator<= (basic_iterator<
        maybe_const> const& rhs) const
    {
        return ptr_ <= rhs.ptr_;
    }

    template <bool maybe_const>
    bool
    operator>= (basic_iterator<
        maybe_const> const& rhs) const
    {
        return ptr_ >= rhs.ptr_;
    }

    basic_iterator&
    operator++()
    {
        ptr_ = increment(1);
        return *this;
    }

    basic_iterator
    operator++ (int) const
    {
        basic_iterator _(*this);
        ++(*this);
        return _;
    }

    basic_iterator&
    operator--()
    {
        ptr_ = increment(-1);
        return *this;
    }

    basic_iterator
    operator-- (int)
    {
        basic_iterator _(*this);
        --(*this);
        return _;
    }

    basic_iterator&
    operator+= (std::ptrdiff_t n)
    {
        ptr_ = increment(n);
        return *this;
    }

    basic_iterator&
    operator-= (std::ptrdiff_t n)
    {
        ptr_ = increment(-n);
        return *this;
    }

    friend
    basic_iterator
    operator+ (std::ptrdiff_t n,
        basic_iterator const& rhs)
    {
        basic_iterator _(rhs);
        _ += n;
        return _;
    }

    friend
    basic_iterator
    operator+ (basic_iterator const& lhs,
        std::ptrdiff_t n)
    {
        return basic_iterator(
            lhs.increment(n), lhs.aligned_size_);
    }

    friend
    basic_iterator
    operator- (basic_iterator const& lhs,
        std::ptrdiff_t n)
    {
        return basic_iterator(
            lhs.increment(-n), lhs.aligned_size_);
    }

    friend
    std::ptrdiff_t
    operator- (basic_iterator const& lhs,
                basic_iterator const& rhs)
    {
        assert(lhs.aligned_size_ == rhs.aligned_size_);
        return
            (reinterpret_cast<byte_t>(lhs.ptr_) -
                reinterpret_cast<byte_t>(rhs.ptr_)) /
                    lhs.aligned_size_;
    }

private:
    friend class vectum;

    basic_iterator (ptr_t ptr,
            std::size_t aligned_size)
        : ptr_ (ptr)
        , aligned_size_ (aligned_size)
    {
    }

    ptr_t
    increment (std::ptrdiff_t n) const
    {
        return reinterpret_cast<ptr_t> (
            (reinterpret_cast<byte_t>(ptr_) +
                n * aligned_size_));
    }
};

//------------------------------------------------------------------------------

template <class T>
vectum<T>::vectum (size_type value_size)
    : value_size_ (value_size)
    , aligned_size_ (((value_size +
        std::alignment_of<T>::value - 1) /
            std::alignment_of<T>::value) *
                std::alignment_of<T>::value)
{
}

template <class T>
vectum<T>&
vectum<T>::operator= (vectum&& other)
{
    size_ = other.size_;
    capacity_ = other.capacity_;
    value_size_ = other.value_size_;
    aligned_size_ = other.aligned_size_;
    buf_ = std::move(other.buf_);
    other.size_ = 0;
    other.capacity_ = 0;
    other.value_size_ = 0;
    other.aligned_size_ = 0;
    return *this;
}

template <class T>
void
vectum<T>::reserve (size_type new_cap)
{
    if (new_cap < capacity_)
        return;
    std::unique_ptr <std::uint8_t[]> buf (
        new std::uint8_t[new_cap * aligned_size_]);
    std::memcpy(buf.get(), buf_.get(),
        size_ * aligned_size_);
    std::swap(buf_, buf);
    capacity_ = new_cap;
}

template <class T>
void
vectum<T>::shrink_to_fit()
{
    if (capacity_ <= size_)
        return;
    if (size_ == 0)
        return reset();
    std::unique_ptr <std::uint8_t[]> buf (
        new std::uint8_t[size_ * aligned_size_]);
    std::memcpy(buf.get(), buf_.get(),
        size_ * aligned_size_);
    std::swap(buf_, buf);
    capacity_ = size_;
}

template <class T>
void
vectum<T>::clear()
{    
    destroy(std::is_trivially_destructible<T>());
    size_ = 0;
}

template <class T>
template <bool is_const, class... Args>
auto
vectum<T>::emplace (basic_iterator<is_const> const& pos,
    Args&&... args) ->
        iterator
{
    std::size_t const index = indexof(pos);
    if (capacity_ > size_)
    {
        std::memmove(element(index+1), element(index),
            (size_ - index) * aligned_size_);
        ::new(element(index)) value_type(
            std::forward<Args>(args)...);
        ++size_;
    }
    else if (capacity_ > 0)
    {
        capacity_ *= 2;
        std::unique_ptr<std::uint8_t[]> buf (
            new std::uint8_t[capacity_ * aligned_size_]);
        ::new(buf.get() + index * aligned_size_) value_type(
            std::forward<Args>(args)...);
        std::memcpy (buf.get(), buf_.get(),
            index * aligned_size_);
        std::memcpy (
            buf.get() + (index + 1) * aligned_size_,
                buf_.get() + index * aligned_size_,
                    (size_ - index) * aligned_size_);
        std::swap(buf_, buf);
        ++size_;
    }
    else
    {
        size_ = 1;
        capacity_ = 1;
        buf_.reset (new std::uint8_t[
            capacity_ * aligned_size_]);
        ::new(buf_.get()) value_type(
            std::forward<Args>(args)...);
    }
    return iterator(element(index), aligned_size_);
}

template <class T>
void
swap (vectum<T>& lhs, vectum<T>& rhs)
{
    std::swap(lhs.buf_, rhs.buf_);
    std::swap(lhs.size_, rhs.size_);
    std::swap(lhs.capacity_, rhs.capacity_);
    std::swap(lhs.value_size_, rhs.value_size_);
    std::swap(lhs.aligned_size_, rhs.aligned_size_);
}

template <class T>
void
vectum<T>::reset()
{
    clear();
    buf_.reset(nullptr);
    capacity_ = 0;
}

} // detail
} // beast

#endif
