//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_BASIC_HEADERS_IPP
#define BEAST_HTTP_IMPL_BASIC_HEADERS_IPP

#include <beast/http/detail/rfc7230.hpp>
#include <algorithm>

namespace beast {
namespace http {

namespace detail {

inline
auto
basic_headers_base::begin() const ->
    const_iterator
{
    return list_.cbegin();
}

inline
auto
basic_headers_base::end() const ->
    const_iterator
{
    return list_.cend();
}

inline
auto
basic_headers_base::cbegin() const ->
    const_iterator
{
    return list_.cbegin();
}

inline
auto
basic_headers_base::cend() const ->
    const_iterator
{
    return list_.cend();
}

} // detail

//------------------------------------------------------------------------------

template<class Allocator>
void
basic_headers<Allocator>::
delete_all()
{
    for(auto it = list_.begin(); it != list_.end();)
    {
        auto& e = *it++;
        alloc_traits::destroy(this->member(), &e);
        alloc_traits::deallocate(
            this->member(), &e, 1);
    }
}

template<class Allocator>
inline
void
basic_headers<Allocator>::
move_assign(basic_headers& other, std::false_type)
{
    if(this->member() != other.member())
    {
        copy_from(other);
        other.clear();
    }
    else
    {
        set_ = std::move(other.set_);
        list_ = std::move(other.list_);
    }
}

template<class Allocator>
inline
void
basic_headers<Allocator>::
move_assign(basic_headers& other, std::true_type)
{
    this->member() = std::move(other.member());
    set_ = std::move(other.set_);
    list_ = std::move(other.list_);
}

template<class Allocator>
inline
void
basic_headers<Allocator>::
copy_assign(basic_headers const& other, std::false_type)
{
    copy_from(other);
}

template<class Allocator>
inline
void
basic_headers<Allocator>::
copy_assign(basic_headers const& other, std::true_type)
{
    this->member() = other.member();
    copy_from(other);
}

//------------------------------------------------------------------------------

template<class Allocator>
basic_headers<Allocator>::
~basic_headers()
{
    delete_all();
}

template<class Allocator>
basic_headers<Allocator>::
basic_headers(Allocator const& alloc)
    : beast::detail::empty_base_optimization<
        alloc_type>(alloc)
{
}

template<class Allocator>
basic_headers<Allocator>::
basic_headers(basic_headers&& other)
    : beast::detail::empty_base_optimization<alloc_type>(
        std::move(other.member()))
    , detail::basic_headers_base(
        std::move(other.set_), std::move(other.list_))
{
}

template<class Allocator>
auto
basic_headers<Allocator>::
operator=(basic_headers&& other) ->
    basic_headers&
{
    if(this == &other)
        return *this;
    clear();
    move_assign(other, std::integral_constant<bool,
        alloc_traits::propagate_on_container_move_assignment::value>{});
    return *this;
}

template<class Allocator>
basic_headers<Allocator>::
basic_headers(basic_headers const& other)
    : basic_headers(alloc_traits::
        select_on_container_copy_construction(other.member()))
{
    copy_from(other);
}

template<class Allocator>
auto
basic_headers<Allocator>::
operator=(basic_headers const& other) ->
    basic_headers&
{
    clear();
    copy_assign(other, std::integral_constant<bool,
        alloc_traits::propagate_on_container_copy_assignment::value>{});
    return *this;
}

template<class Allocator>
template<class OtherAlloc>
basic_headers<Allocator>::
basic_headers(basic_headers<OtherAlloc> const& other)
{
    copy_from(other);
}

template<class Allocator>
template<class OtherAlloc>
auto
basic_headers<Allocator>::
operator=(basic_headers<OtherAlloc> const& other) ->
    basic_headers&
{
    clear();
    copy_from(other);
    return *this;
}

template<class Allocator>
template<class FwdIt>
basic_headers<Allocator>::
basic_headers(FwdIt first, FwdIt last)
{
    for(;first != last; ++first)
        insert(first->name(), first->value());
}

template<class Allocator>
std::size_t
basic_headers<Allocator>::
count(boost::string_ref const& name) const
{
    auto const it = set_.find(name, less{});
    if(it == set_.end())
        return 0;
    auto const last = set_.upper_bound(name, less{});
    return static_cast<std::size_t>(std::distance(it, last));
}

template<class Allocator>
auto
basic_headers<Allocator>::
find(boost::string_ref const& name) const ->
    iterator
{
    auto const it = set_.find(name, less{});
    if(it == set_.end())
        return list_.end();
    return list_.iterator_to(*it);
}

template<class Allocator>
boost::string_ref
basic_headers<Allocator>::
operator[](boost::string_ref const& name) const
{
    auto const it = find(name);
    if(it == end())
        return {};
    return it->second;
}

template<class Allocator>
void
basic_headers<Allocator>::
clear() noexcept
{
    delete_all();
    list_.clear();
    set_.clear();
}

template<class Allocator>
std::size_t
basic_headers<Allocator>::
erase(boost::string_ref const& name)
{
    auto it = set_.find(name, less{});
    if(it == set_.end())
        return 0;
    auto const last = set_.upper_bound(name, less{});
    std::size_t n = 1;
    for(;;)
    {
        auto& e = *it++;
        set_.erase(set_.iterator_to(e));
        list_.erase(list_.iterator_to(e));
        alloc_traits::destroy(this->member(), &e);
        alloc_traits::deallocate(this->member(), &e, 1);
        if(it == last)
            break;
        ++n;
    }
    return n;
}

template<class Allocator>
void
basic_headers<Allocator>::
insert(boost::string_ref const& name,
    boost::string_ref value)
{
    value = detail::trim(value);
    auto const p = alloc_traits::allocate(this->member(), 1);
    alloc_traits::construct(this->member(), p, name, value);
    set_.insert_before(set_.upper_bound(name, less{}), *p);
    list_.push_back(*p);
}

template<class Allocator>
void
basic_headers<Allocator>::
replace(boost::string_ref const& name,
    boost::string_ref value)
{
    value = detail::trim(value);
    erase(name);
    insert(name, value);
}

} // http
} // beast

#endif
