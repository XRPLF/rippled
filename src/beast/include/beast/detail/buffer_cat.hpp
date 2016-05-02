//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_BUFFER_CAT_HPP
#define BEAST_DETAIL_BUFFER_CAT_HPP

#include <boost/asio/buffer.hpp>
#include <cstdint>
#include <iterator>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace beast {
namespace detail {

template<class ValueType, class... Bs>
class buffer_cat_helper
{
    std::tuple<Bs...> bs_;

public:
    using value_type = ValueType;

    class const_iterator;

    buffer_cat_helper(buffer_cat_helper&&) = default;
    buffer_cat_helper(buffer_cat_helper const&) = default;
    buffer_cat_helper& operator=(buffer_cat_helper&&) = default;
    buffer_cat_helper& operator=(buffer_cat_helper const&) = default;

    explicit
    buffer_cat_helper(Bs const&... bs)
        : bs_(bs...)
    {
    }

    const_iterator
    begin() const;

    const_iterator
    end() const;
};

template<class U>
std::size_t constexpr
max_sizeof()
{
    return sizeof(U);
}

template<class U0, class U1, class... Us>
std::size_t constexpr
max_sizeof()
{
    return
        max_sizeof<U0>() > max_sizeof<U1, Us...>() ?
        max_sizeof<U0>() : max_sizeof<U1, Us...>();
}

template<class ValueType, class... Bs>
class buffer_cat_helper<
    ValueType, Bs...>::const_iterator
{
    std::size_t n_;
    std::tuple<Bs...> const* bs_;
    std::array<std::uint8_t,
        max_sizeof<typename Bs::const_iterator...>()> buf_;

    friend class buffer_cat_helper<ValueType, Bs...>;

    template<std::size_t I>
    using C = std::integral_constant<std::size_t, I>;

    template<std::size_t I>
    using iter_t = typename std::tuple_element<
        I, std::tuple<Bs...>>::type::const_iterator;

    template<std::size_t I>
    iter_t<I>&
    iter()
    {
        return *reinterpret_cast<
            iter_t<I>*>(buf_.data());
    }

    template<std::size_t I>
    iter_t<I> const&
    iter() const
    {
        return *reinterpret_cast<
            iter_t<I> const*>(buf_.data());
    }

public:
    using value_type = ValueType;
    using pointer = value_type const*;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::bidirectional_iterator_tag;

    ~const_iterator();
    const_iterator();
    const_iterator(const_iterator&& other);
    const_iterator(const_iterator const& other);
    const_iterator& operator=(const_iterator&& other);
    const_iterator& operator=(const_iterator const& other);

    bool
    operator==(const_iterator const& other) const;

    bool
    operator!=(const_iterator const& other) const
    {
        return !(*this == other);
    }

    reference
    operator*() const;

    pointer
    operator->() const = delete;

    const_iterator&
    operator++();

    const_iterator
    operator++(int)
    {
        auto temp = *this;
        ++(*this);
        return temp;
    }

    const_iterator&
    operator--();

    const_iterator
    operator--(int)
    {
        auto temp = *this;
        --(*this);
        return temp;
    }

private:
    const_iterator(
        std::tuple<Bs...> const& bs, bool at_end);

    void
    construct(C<sizeof...(Bs)>)
    {
        auto constexpr I = sizeof...(Bs);
        n_ = I;
    }

    template<std::size_t I>
    void
    construct(C<I>)
    {
        if(std::get<I>(*bs_).begin() !=
            std::get<I>(*bs_).end())
        {
            n_ = I;
            new(buf_.data()) iter_t<I>{
                std::get<I>(*bs_).begin()};
            return;
        }
        construct(C<I+1>{});
    }

    void
    destroy(C<sizeof...(Bs)>)
    {
        return;
    }

    template<std::size_t I>
    void
    destroy(C<I>)
    {
        if(n_ == I)
        {
            using Iter = iter_t<I>;
            iter<I>().~Iter();
            return;
        }
        destroy(C<I+1>{});
    }

    void
    move(C<sizeof...(Bs)>, const_iterator&&)
    {
        return;
    }

    template<std::size_t I>
    void
    move(C<I>, const_iterator&& other)
    {
        if(n_ == I)
        {
            new(buf_.data()) iter_t<I>{
                std::move(other.iter<I>())};
            return;
        }
        move(C<I+1>{}, std::move(other));
    }

    void
    copy(C<sizeof...(Bs)>, const_iterator const&)
    {
        return;
    }

    template<std::size_t I>
    void
    copy(C<I>, const_iterator const& other)
    {
        if(n_ == I)
        {
            new(buf_.data()) iter_t<I>{
                other.iter<I>()};
            return;
        }
        copy(C<I+1>{}, other);
    }

    bool
    equal(C<sizeof...(Bs)>,
        const_iterator const&) const
    {
        return true;
    }

    template<std::size_t I>
    bool
    equal(C<I>, const_iterator const& other) const
    {
        if(n_ == I)
            return iter<I>() == other.iter<I>();
        return equal(C<I+1>{}, other);
    }

    [[noreturn]]
    reference
    dereference(C<sizeof...(Bs)>) const
    {
        throw std::logic_error("invalid iterator");
    }

    template<std::size_t I>
    reference
    dereference(C<I>) const
    {
        if(n_ == I)
            return *iter<I>();
        return dereference(C<I+1>{});
    }

    [[noreturn]]
    void
    increment(C<sizeof...(Bs)>)
    {
        throw std::logic_error("invalid iterator");
    }

    template<std::size_t I>
    void
    increment(C<I>)
    {
        if(n_ == I)
        {
            if(++iter<I>() !=
                    std::get<I>(*bs_).end())
                return;
            using Iter = iter_t<I>;
            iter<I>().~Iter();
            return construct(C<I+1>{});
        }
        increment(C<I+1>{});
    }

    void
    decrement(C<sizeof...(Bs)>)
    {
        auto constexpr I = sizeof...(Bs);
        if(n_ == I)
        {
            --n_;
            new(buf_.data()) iter_t<I-1>{
                std::get<I-1>(*bs_).end()};
        }
        decrement(C<I-1>{});
    }

    void
    decrement(C<0>)
    {
        auto constexpr I = 0;
        if(iter<I>() != std::get<I>(*bs_).begin())
        {
            --iter<I>();
            return;
        }
        throw std::logic_error("invalid iterator");
    }

    template<std::size_t I>
    void
    decrement(C<I>)
    {
        if(n_ == I)
        {
            if(iter<I>() != std::get<I>(*bs_).begin())
            {
                --iter<I>();
                return;
            }
            --n_;
            using Iter = iter_t<I>;
            iter<I>().~Iter();
            new(buf_.data()) iter_t<I-1>{
                std::get<I-1>(*bs_).end()};
        }
        decrement(C<I-1>{});
    }
};

//------------------------------------------------------------------------------

template<class ValueType, class... Bs>
buffer_cat_helper<ValueType, Bs...>::
const_iterator::~const_iterator()
{
    destroy(C<0>{});
}

template<class ValueType, class... Bs>
buffer_cat_helper<ValueType, Bs...>::
const_iterator::const_iterator()
    : n_(sizeof...(Bs))
    , bs_(nullptr)
{
}

template<class ValueType, class... Bs>
buffer_cat_helper<ValueType, Bs...>::
const_iterator::const_iterator(
    std::tuple<Bs...> const& bs, bool at_end)
    : bs_(&bs)
{
    if(at_end)
        n_ = sizeof...(Bs);
    else
        construct(C<0>{});
}

template<class ValueType, class... Bs>
buffer_cat_helper<ValueType, Bs...>::
const_iterator::const_iterator(const_iterator&& other)
    : n_(other.n_)
    , bs_(other.bs_)
{
    move(C<0>{}, std::move(other));
}

template<class ValueType, class... Bs>
buffer_cat_helper<ValueType, Bs...>::
const_iterator::const_iterator(const_iterator const& other)
    : n_(other.n_)
    , bs_(other.bs_)
{
    copy(C<0>{}, other);
}

template<class ValueType, class... Bs>
auto
buffer_cat_helper<ValueType, Bs...>::
const_iterator::operator=(const_iterator&& other) ->
    const_iterator&
{
    if(&other == this)
        return *this;
    destroy(C<0>{});
    n_ = other.n_;
    bs_ = other.bs_;
    move(C<0>{}, std::move(other));
    return *this;
}

template<class ValueType, class... Bs>
auto
buffer_cat_helper<ValueType, Bs...>::
const_iterator::operator=(const_iterator const& other) ->
const_iterator&
{
    if(&other == this)
        return *this;
    destroy(C<0>{});
    n_ = other.n_;
    bs_ = other.bs_;
    copy(C<0>{}, other);
    return *this;
}

template<class ValueType, class... Bs>
bool
buffer_cat_helper<ValueType, Bs...>::
const_iterator::operator==(const_iterator const& other) const
{
    if(bs_ != other.bs_)
        return false;
    if(n_ != other.n_)
        return false;
    return equal(C<0>{}, other);
}

template<class ValueType, class... Bs>
auto
buffer_cat_helper<ValueType, Bs...>::
const_iterator::operator*() const ->
    reference
{
    return dereference(C<0>{});
}

template<class ValueType, class... Bs>
auto
buffer_cat_helper<ValueType, Bs...>::
const_iterator::operator++() ->
    const_iterator&
{
    increment(C<0>{});
    return *this;
}

template<class ValueType, class... Bs>
auto
buffer_cat_helper<ValueType, Bs...>::
const_iterator::operator--() ->
    const_iterator&
{
    decrement(C<sizeof...(Bs)>{});
    return *this;
}

template<class ValueType, class... Bs>
auto
buffer_cat_helper<ValueType, Bs...>::begin() const ->
    const_iterator
{
    return const_iterator(bs_, false);
}

template<class ValueType, class... Bs>
auto
buffer_cat_helper<ValueType, Bs...>::end() const ->
    const_iterator
{
    return const_iterator(bs_, true);
}

} // detail
} // beast

#endif
