//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_DETAIL_BUFFER_CAT_HPP
#define BEAST_DETAIL_BUFFER_CAT_HPP

#include <beast/core/buffer_concepts.hpp>
#include <beast/core/detail/type_traits.hpp>
#include <boost/asio/buffer.hpp>
#include <cstdint>
#include <iterator>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace beast {
namespace detail {

template<class... Bn>
struct common_buffers_type
{
    using type = typename std::conditional<
        std::is_convertible<std::tuple<Bn...>,
            typename repeat_tuple<sizeof...(Bn),
                boost::asio::mutable_buffer>::type>::value,
                    boost::asio::mutable_buffer,
                        boost::asio::const_buffer>::type;
};

template<class... Bn>
class buffer_cat_helper
{
    std::tuple<Bn...> bn_;

public:
    using value_type = typename
        common_buffers_type<Bn...>::type;

    class const_iterator;

    buffer_cat_helper(buffer_cat_helper&&) = default;
    buffer_cat_helper(buffer_cat_helper const&) = default;
    buffer_cat_helper& operator=(buffer_cat_helper&&) = delete;
    buffer_cat_helper& operator=(buffer_cat_helper const&) = delete;

    explicit
    buffer_cat_helper(Bn const&... bn)
        : bn_(bn...)
    {
    }

    const_iterator
    begin() const;

    const_iterator
    end() const;
};

template<class... Bn>
class buffer_cat_helper<Bn...>::const_iterator
{
    std::size_t n_;
    std::tuple<Bn...> const* bn_;
    std::array<std::uint8_t,
        max_sizeof<typename Bn::const_iterator...>()> buf_;

    friend class buffer_cat_helper<Bn...>;

    template<std::size_t I>
    using C = std::integral_constant<std::size_t, I>;

    template<std::size_t I>
    using iter_t = typename std::tuple_element<
        I, std::tuple<Bn...>>::type::const_iterator;

    template<std::size_t I>
    iter_t<I>&
    iter()
    {
        // type-pun
        return *reinterpret_cast<
            iter_t<I>*>(static_cast<void*>(
                buf_.data()));
    }

    template<std::size_t I>
    iter_t<I> const&
    iter() const
    {
        // type-pun
        return *reinterpret_cast<
            iter_t<I> const*>(static_cast<
                void const*>(buf_.data()));
    }

public:
    using value_type = typename
        common_buffers_type<Bn...>::type;
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
        std::tuple<Bn...> const& bn, bool at_end);

    void
    construct(C<sizeof...(Bn)> const&)
    {
        auto constexpr I = sizeof...(Bn);
        n_ = I;
    }

    template<std::size_t I>
    void
    construct(C<I> const&)
    {
        if(std::get<I>(*bn_).begin() !=
            std::get<I>(*bn_).end())
        {
            n_ = I;
            new(buf_.data()) iter_t<I>{
                std::get<I>(*bn_).begin()};
            return;
        }
        construct(C<I+1>{});
    }

    void
    destroy(C<sizeof...(Bn)> const&)
    {
        return;
    }

    template<std::size_t I>
    void
    destroy(C<I> const&)
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
    move(const_iterator&&,
        C<sizeof...(Bn)> const&)
    {
    }

    template<std::size_t I>
    void
    move(const_iterator&& other,
        C<I> const&)
    {
        if(n_ == I)
        {
            new(buf_.data()) iter_t<I>{
                std::move(other.iter<I>())};
            return;
        }
        move(std::move(other), C<I+1>{});
    }

    void
    copy(const_iterator const&,
        C<sizeof...(Bn)> const&)
    {
    }

    template<std::size_t I>
    void
    copy(const_iterator const& other,
        C<I> const&)
    {
        if(n_ == I)
        {
            new(buf_.data()) iter_t<I>{
                other.iter<I>()};
            return;
        }
        copy(other, C<I+1>{});
    }

    bool
    equal(const_iterator const&,
        C<sizeof...(Bn)> const&) const
    {
        return true;
    }

    template<std::size_t I>
    bool
    equal(const_iterator const& other,
        C<I> const&) const
    {
        if(n_ == I)
            return iter<I>() == other.iter<I>();
        return equal(other, C<I+1>{});
    }

    [[noreturn]]
    reference
    dereference(C<sizeof...(Bn)> const&) const
    {
        throw detail::make_exception<std::logic_error>(
            "invalid iterator", __FILE__, __LINE__);
    }

    template<std::size_t I>
    reference
    dereference(C<I> const&) const
    {
        if(n_ == I)
            return *iter<I>();
        return dereference(C<I+1>{});
    }

    [[noreturn]]
    void
    increment(C<sizeof...(Bn)> const&)
    {
        throw detail::make_exception<std::logic_error>(
            "invalid iterator", __FILE__, __LINE__);
    }

    template<std::size_t I>
    void
    increment(C<I> const&)
    {
        if(n_ == I)
        {
            if(++iter<I>() !=
                    std::get<I>(*bn_).end())
                return;
            using Iter = iter_t<I>;
            iter<I>().~Iter();
            return construct(C<I+1>{});
        }
        increment(C<I+1>{});
    }

    void
    decrement(C<sizeof...(Bn)> const&)
    {
        auto constexpr I = sizeof...(Bn);
        if(n_ == I)
        {
            --n_;
            new(buf_.data()) iter_t<I-1>{
                std::get<I-1>(*bn_).end()};
        }
        decrement(C<I-1>{});
    }

    void
    decrement(C<0> const&)
    {
        auto constexpr I = 0;
        if(iter<I>() != std::get<I>(*bn_).begin())
        {
            --iter<I>();
            return;
        }
        throw detail::make_exception<std::logic_error>(
            "invalid iterator", __FILE__, __LINE__);
    }

    template<std::size_t I>
    void
    decrement(C<I> const&)
    {
        if(n_ == I)
        {
            if(iter<I>() != std::get<I>(*bn_).begin())
            {
                --iter<I>();
                return;
            }
            --n_;
            using Iter = iter_t<I>;
            iter<I>().~Iter();
            new(buf_.data()) iter_t<I-1>{
                std::get<I-1>(*bn_).end()};
        }
        decrement(C<I-1>{});
    }
};

//------------------------------------------------------------------------------

template<class... Bn>
buffer_cat_helper<Bn...>::
const_iterator::~const_iterator()
{
    destroy(C<0>{});
}

template<class... Bn>
buffer_cat_helper<Bn...>::
const_iterator::const_iterator()
    : n_(sizeof...(Bn))
    , bn_(nullptr)
{
}

template<class... Bn>
buffer_cat_helper<Bn...>::
const_iterator::const_iterator(
    std::tuple<Bn...> const& bn, bool at_end)
    : bn_(&bn)
{
    if(at_end)
        n_ = sizeof...(Bn);
    else
        construct(C<0>{});
}

template<class... Bn>
buffer_cat_helper<Bn...>::
const_iterator::const_iterator(const_iterator&& other)
    : n_(other.n_)
    , bn_(other.bn_)
{
    move(std::move(other), C<0>{});
}

template<class... Bn>
buffer_cat_helper<Bn...>::
const_iterator::const_iterator(const_iterator const& other)
    : n_(other.n_)
    , bn_(other.bn_)
{
    copy(other, C<0>{});
}

template<class... Bn>
auto
buffer_cat_helper<Bn...>::
const_iterator::operator=(const_iterator&& other) ->
    const_iterator&
{
    if(&other == this)
        return *this;
    destroy(C<0>{});
    n_ = other.n_;
    bn_ = other.bn_;
    move(std::move(other), C<0>{});
    return *this;
}

template<class... Bn>
auto
buffer_cat_helper<Bn...>::
const_iterator::operator=(const_iterator const& other) ->
const_iterator&
{
    if(&other == this)
        return *this;
    destroy(C<0>{});
    n_ = other.n_;
    bn_ = other.bn_;
    copy(other, C<0>{});
    return *this;
}

template<class... Bn>
bool
buffer_cat_helper<Bn...>::
const_iterator::operator==(const_iterator const& other) const
{
    if(bn_ != other.bn_)
        return false;
    if(n_ != other.n_)
        return false;
    return equal(other, C<0>{});
}

template<class... Bn>
auto
buffer_cat_helper<Bn...>::
const_iterator::operator*() const ->
    reference
{
    return dereference(C<0>{});
}

template<class... Bn>
auto
buffer_cat_helper<Bn...>::
const_iterator::operator++() ->
    const_iterator&
{
    increment(C<0>{});
    return *this;
}

template<class... Bn>
auto
buffer_cat_helper<Bn...>::
const_iterator::operator--() ->
    const_iterator&
{
    decrement(C<sizeof...(Bn)>{});
    return *this;
}

template<class... Bn>
inline
auto
buffer_cat_helper<Bn...>::begin() const ->
    const_iterator
{
    return const_iterator{bn_, false};
}

template<class... Bn>
inline
auto
buffer_cat_helper<Bn...>::end() const ->
    const_iterator
{
    return const_iterator{bn_, true};
}

} // detail
} // beast

#endif
