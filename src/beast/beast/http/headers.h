//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_HTTP_HEADERS_H_INCLUDED
#define BEAST_HTTP_HEADERS_H_INCLUDED

#include <beast/http/detail/writes.h>
#include <beast/asio/type_check.h>
#include <beast/utility/ci_char_traits.h>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <beast/cxx17/type_traits.h> // <type_traits>
#include <utility>

namespace beast {
namespace http {

template<class Allocator>
class headers
{
public:
    using value_type = std::pair<std::string, std::string>;

private:
    struct element
        : boost::intrusive::set_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>>
        , boost::intrusive::list_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>>
    {
        value_type data;

        element(std::string const& f, std::string const& v)
        {
            data.first = f;
            data.second = v;
        }
    };

    struct less : private ci_less
    {
        template<class String>
        bool
        operator()(String const& lhs, element const& rhs) const
        {
            return ci_less::operator()(lhs, rhs.data.first);
        }

        template<class String>
        bool
        operator()(element const& lhs, String const& rhs) const
        {
            return ci_less::operator()(lhs.data.first, rhs);
        }

        bool
        operator()(element const& lhs, element const& rhs) const
        {
            return beast::ci_less::operator()(
                lhs.data.first, rhs.data.first);
        }
    };

    struct transform
        : public std::unary_function<element, value_type>
    {
        value_type const&
        operator()(element const& e) const
        {
            return e.data;
        }
    };

    using list_t = typename boost::intrusive::make_list<
        element, boost::intrusive::constant_time_size<false>>::type;

    using set_t = typename boost::intrusive::make_set<
        element, boost::intrusive::constant_time_size<true>,
            boost::intrusive::compare<less>>::type;

    list_t list_;
    set_t set_;

public:
    using iterator = boost::transform_iterator<transform,
        typename list_t::const_iterator>;

    using const_iterator = iterator;

    ~headers();

    headers()
    {
        static_assert(std::is_copy_constructible<headers>::value, "");
        static_assert(std::is_move_constructible<headers>::value, "");
        static_assert(std::is_copy_assignable<headers>::value, "");
        static_assert(std::is_move_assignable<headers>::value, "");
    }

    /** Move constructor.

        After the move, the moved-from object has an empty list of fields.
    */
    headers(headers&&);

    /** Move assignment.

        After the move, the moved-from object has an empty list of fields.
    */
    headers& operator=(headers&&);

    /** Copy constructor. */
    headers(headers const&);

    /** Copy assignment. */
    headers& operator=(headers const&);

    /** Returns an iterator to fields in order of appearance. */
    /** @{ */
    iterator
    begin() const
    {
        return {list_.cbegin(), transform{}};
    }

    iterator
    end() const
    {
        return {list_.cend(), transform{}};
    }

    iterator
    cbegin() const
    {
        return {list_.cbegin(), transform{}};
    }

    iterator
    cend() const
    {
        return {list_.cend(), transform{}};
    }
    /** @} */

    /** Returns `true` if the specified field exists. */
    bool
    exists(std::string const& field) const
    {
        return set_.find(field, less{}) != set_.end();
    }

    /** Returns an iterator to the case-insensitive matching header. */
    iterator
    find(std::string const& field) const;

    /** Returns the value for a case-insensitive matching header, or "" */
    std::string const&
    operator[](std::string const& field) const;

    /** Clear the contents of the headers. */
    void
    clear() noexcept;

    /** Remove a field.

        @return The number of fields removed.
    */
    std::size_t
    erase(std::string const& field);

    /** Insert a field value.

        If a field value already exists the new value will be
        extended as per RFC2616 Section 4.2.
    */
    // VFALCO TODO Consider allowing rvalue references for std::move?
    void
    insert(std::string const& field, std::string const& value);

    /** Insert a field value.

        If a field value already exists the new value will be
        extended as per RFC2616 Section 4.2.
    */
    template<class T,
        class = std::enable_if_t<
            ! std::is_constructible<std::string, T>::value>>
    void
    insert(std::string const& field, T&& t)
    {
        insert(field, boost::lexical_cast<std::string>(
            std::forward<T>(t)));
    }

    /** Replace a field value.

        The current field value, if any, is removed. Then the
        specified value is inserted as if by insert(field, value).
    */
    void
    replace(std::string const& field, std::string const& value);

    /** Replace a field value.

        The current field value, if any, is removed. Then the
        specified value is inserted as if by insert(field, value).
    */
    template<class T,
        class = std::enable_if_t<
            ! std::is_constructible<std::string, T>::value>>
    void
    replace(std::string const& field, T&& t)
    {
        replace(field, boost::lexical_cast<std::string>(
            std::forward<T>(t)));
    }

    /** Write the headers to a Streambuf. */
    template<class Streambuf>
    void
    write(Streambuf& streambuf) const;
};

template<class Allocator>
headers<Allocator>::~headers()
{
    clear();
}

template<class Allocator>
headers<Allocator>::headers(headers&& other)
    : list_(std::move(other.list_))
    , set_(std::move(other.set_))
{
    other.list_.clear();
    other.set_.clear();
}

template<class Allocator>
auto
headers<Allocator>::operator=(headers&& other) ->
    headers&
{
    list_ = std::move(other.list_);
    set_ = std::move(other.set_);
    other.list_.clear();
    other.set_.clear();
    return *this;
}

template<class Allocator>
headers<Allocator>::headers(headers const& other)
{
    for (auto const& e : other.list_)
        insert(e.data.first, e.data.second);
}

template<class Allocator>
auto
headers<Allocator>::operator= (headers const& other) ->
    headers&
{
    clear();
    for (auto const& e : other.list_)
        insert (e.data.first, e.data.second);
    return *this;
}

template<class Allocator>
auto
headers<Allocator>::find(std::string const& field) const ->
    iterator
{
    auto const it = set_.find(field, less{});
    if(it == set_.end())
        return {list_.end(), transform{}};
    return {list_.iterator_to(*it), transform{}};
}

template<class Allocator>
std::string const&
headers<Allocator>::operator[](std::string const& field) const
{
    // VFALCO This none object looks sketchy
    static std::string const none;
    auto const it = find(field);
    if(it == end())
        return none;
    return it->second;
}

template<class Allocator>
void
headers<Allocator>::clear() noexcept
{
    for(auto it = list_.begin(); it != list_.end();)
        delete &(*it++);
}

template<class Allocator>
std::size_t
headers<Allocator>::erase(std::string const& field)
{
    auto const it = set_.find(field, less{});
    if(it == set_.end())
        return 0;
    auto& e = *it;
    set_.erase(set_.iterator_to(e));
    list_.erase(list_.iterator_to(e));
    delete &e;
    return 1;
}

template<class Allocator>
void
headers<Allocator>::insert(
    std::string const& field, std::string const& value)
{
    typename set_t::insert_commit_data d;
    auto const result = set_.insert_check (field, less{}, d);
    if (result.second)
    {
        auto const p = new element (field, value);
        list_.push_back(*p);
        set_.insert_commit(*p, d);
        return;
    }
    // If field already exists, insert comma
    // separated value as per RFC2616 section 4.2
    auto& cur = result.first->data.second;
    cur.reserve(cur.size() + 1 + value.size());
    cur.append(1, ',');
    cur.append(value);
}

template<class Allocator>
void
headers<Allocator>::replace(
    std::string const& field, std::string const& value)
{
    erase(field);
    insert(field, value);
}

template<class Allocator>
template<class Streambuf>
void
headers<Allocator>::write(Streambuf& streambuf) const
{
    static_assert(is_Streambuf<Streambuf>::value,
        "Streambuf requirements not met");
    for (auto const& e : list_)
    {
        detail::write(streambuf, e.data.first);
        detail::write(streambuf, ": ");
        detail::write(streambuf, e.data.second);
        detail::write(streambuf, "\r\n");
    }
}

using http_headers =
    headers<std::allocator<char>>;

} // http
} // beast

#endif
