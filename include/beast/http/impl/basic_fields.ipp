//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_HTTP_IMPL_BASIC_FIELDS_IPP
#define BEAST_HTTP_IMPL_BASIC_FIELDS_IPP

#include <beast/http/detail/rfc7230.hpp>
#include <algorithm>

namespace beast {
namespace http {

template<class Allocator>
void
basic_fields<Allocator>::
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
basic_fields<Allocator>::
move_assign(basic_fields& other, std::false_type)
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
basic_fields<Allocator>::
move_assign(basic_fields& other, std::true_type)
{
    this->member() = std::move(other.member());
    set_ = std::move(other.set_);
    list_ = std::move(other.list_);
}

template<class Allocator>
inline
void
basic_fields<Allocator>::
copy_assign(basic_fields const& other, std::false_type)
{
    copy_from(other);
}

template<class Allocator>
inline
void
basic_fields<Allocator>::
copy_assign(basic_fields const& other, std::true_type)
{
    this->member() = other.member();
    copy_from(other);
}

//------------------------------------------------------------------------------

template<class Allocator>
basic_fields<Allocator>::
~basic_fields()
{
    delete_all();
}

template<class Allocator>
basic_fields<Allocator>::
basic_fields(Allocator const& alloc)
    : beast::detail::empty_base_optimization<
        alloc_type>(alloc)
{
}

template<class Allocator>
basic_fields<Allocator>::
basic_fields(basic_fields&& other)
    : beast::detail::empty_base_optimization<alloc_type>(
        std::move(other.member()))
    , detail::basic_fields_base(
        std::move(other.set_), std::move(other.list_))
{
}

template<class Allocator>
auto
basic_fields<Allocator>::
operator=(basic_fields&& other) ->
    basic_fields&
{
    if(this == &other)
        return *this;
    clear();
    move_assign(other, std::integral_constant<bool,
        alloc_traits::propagate_on_container_move_assignment::value>{});
    return *this;
}

template<class Allocator>
basic_fields<Allocator>::
basic_fields(basic_fields const& other)
    : basic_fields(alloc_traits::
        select_on_container_copy_construction(other.member()))
{
    copy_from(other);
}

template<class Allocator>
auto
basic_fields<Allocator>::
operator=(basic_fields const& other) ->
    basic_fields&
{
    clear();
    copy_assign(other, std::integral_constant<bool,
        alloc_traits::propagate_on_container_copy_assignment::value>{});
    return *this;
}

template<class Allocator>
template<class OtherAlloc>
basic_fields<Allocator>::
basic_fields(basic_fields<OtherAlloc> const& other)
{
    copy_from(other);
}

template<class Allocator>
template<class OtherAlloc>
auto
basic_fields<Allocator>::
operator=(basic_fields<OtherAlloc> const& other) ->
    basic_fields&
{
    clear();
    copy_from(other);
    return *this;
}

template<class Allocator>
template<class FwdIt>
basic_fields<Allocator>::
basic_fields(FwdIt first, FwdIt last)
{
    for(;first != last; ++first)
        insert(first->name(), first->value());
}

template<class Allocator>
std::size_t
basic_fields<Allocator>::
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
basic_fields<Allocator>::
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
basic_fields<Allocator>::
operator[](boost::string_ref const& name) const
{
    auto const it = find(name);
    if(it == end())
        return {};
    return it->second;
}

template<class Allocator>
void
basic_fields<Allocator>::
clear() noexcept
{
    delete_all();
    list_.clear();
    set_.clear();
}

template<class Allocator>
std::size_t
basic_fields<Allocator>::
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
basic_fields<Allocator>::
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
basic_fields<Allocator>::
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
