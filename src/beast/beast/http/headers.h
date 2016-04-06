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
#include <cctype>
#include <memory>
#include <string>
#include <beast/cxx17/type_traits.h> // <type_traits>
#include <utility>

namespace beast {
namespace http2 {

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

    headers() = default;

    headers(headers&& other);
    headers(headers const& other);
    headers& operator=(headers&& other);
    headers& operator=(headers const& other);

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

using http_headers =
    headers<std::allocator<char>>;

//------------------------------------------------------------------------------

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

} // http
} // beast

//------------------------------------------------------------------------------

//
// LEGACY
//

#include <beast/utility/ci_char_traits.h>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <ostream>
#include <string>
#include <utility>

namespace beast {
namespace http {

/** Holds a collection of HTTP headers. */
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
        template <class = void>
        element (std::string const& f, std::string const& v);

        value_type data;
    };

    struct less : private beast::ci_less
    {
        template <class String>
        bool
        operator() (String const& lhs, element const& rhs) const;

        template <class String>
        bool
        operator() (element const& lhs, String const& rhs) const;

        bool
        operator() (element const& lhs, element const& rhs) const;
    };

    struct transform
        : public std::unary_function <element, value_type>
    {
        value_type const&
        operator() (element const& e) const
        {
            return e.data;
        }
    };

    using list_t = boost::intrusive::make_list <element,
        boost::intrusive::constant_time_size <false>
            >::type;

    using set_t = boost::intrusive::make_set <element,
        boost::intrusive::constant_time_size <true>,
        boost::intrusive::compare<less>
            >::type;

    list_t list_;
    set_t set_;

public:
    using iterator = boost::transform_iterator <transform,
        list_t::const_iterator>;
    using const_iterator = iterator;

    ~headers()
    {
        clear();
    }

    headers() = default;

    headers (headers&& other);
    headers& operator= (headers&& other);

    headers (headers const& other);
    headers& operator= (headers const& other);

    /** Returns an iterator to headers in order of appearance. */
    /** @{ */
    iterator
    begin() const;

    iterator
    end() const;

    iterator
    cbegin() const;

    iterator
    cend() const;
    /** @} */

    /** Returns `true` if the specified field exists. */
    bool
    exists(std::string const& field) const
    {
        return set_.find(field, less{}) != set_.end();
    }

    /** Returns an iterator to the case-insensitive matching header. */
    template <class = void>
    iterator
    find (std::string const& field) const;

    /** Returns the value for a case-insensitive matching header, or "" */
    template <class = void>
    std::string const&
    operator[] (std::string const& field) const;

    /** Clear the contents of the headers. */
    template <class = void>
    void
    clear() noexcept;

    /** Remove a field.
        @return The number of fields removed.
    */
    template <class = void>
    std::size_t
    erase (std::string const& field);

    /** Append a field value.
        If a field value already exists the new value will be
        extended as per RFC2616 Section 4.2.
    */
    // VFALCO TODO Consider allowing rvalue references for std::move
    template <class = void>
    void
    append (std::string const& field, std::string const& value);
};

template <class = void>
std::string
to_string (headers const& h);

// HACK!
template <class = void>
std::map <std::string, std::string>
build_map (headers const& h);

//------------------------------------------------------------------------------

template <class>
headers::element::element (
    std::string const& f, std::string const& v)
{
    data.first = f;
    data.second = v;
}

template <class String>
bool
headers::less::operator() (
    String const& lhs, element const& rhs) const
{
    return beast::ci_less::operator() (lhs, rhs.data.first);
}

template <class String>
bool
headers::less::operator() (
    element const& lhs, String const& rhs) const
{
    return beast::ci_less::operator() (lhs.data.first, rhs);
}

inline
bool
headers::less::operator() (
    element const& lhs, element const& rhs) const
{
    return beast::ci_less::operator() (lhs.data.first, rhs.data.first);
}

//------------------------------------------------------------------------------

inline
headers::headers (headers&& other)
    : list_ (std::move (other.list_))
    , set_ (std::move (other.set_))
{
    other.list_.clear();
    other.set_.clear();
}

inline
headers&
headers::operator= (headers&& other)
{
    list_ = std::move(other.list_);
    set_ = std::move(other.set_);
    other.list_.clear();
    other.set_.clear();
    return *this;
}

inline
headers::headers (headers const& other)
{
    for (auto const& e : other.list_)
        append (e.data.first, e.data.second);
}

inline
headers&
headers::operator= (headers const& other)
{
    clear();
    for (auto const& e : other.list_)
        append (e.data.first, e.data.second);
    return *this;
}

inline
headers::iterator
headers::begin() const
{
    return {list_.cbegin(), transform{}};
}

inline
headers::iterator
headers::end() const
{
    return {list_.cend(), transform{}};
}

inline
headers::iterator
headers::cbegin() const
{
    return {list_.cbegin(), transform{}};
}

inline
headers::iterator
headers::cend() const
{
    return {list_.cend(), transform{}};
}

template <class>
headers::iterator
headers::find (std::string const& field) const
{
    auto const iter (set_.find (field, less{}));
    if (iter == set_.end())
        return {list_.end(), transform{}};
    return {list_.iterator_to (*iter), transform{}};
}

template <class>
std::string const&
headers::operator[] (std::string const& field) const
{
    static std::string const none;
    auto const it = find(field);
    if(it == end())
        return none;
    return it->second;
}

template <class>
void
headers::clear() noexcept
{
    for (auto iter (list_.begin()); iter != list_.end();)
        delete &(*iter++);
}

template <class>
std::size_t
headers::erase (std::string const& field)
{
    auto const iter = set_.find(field, less{});
    if (iter == set_.end())
        return 0;
    element& e = *iter;
    set_.erase(set_.iterator_to(e));
    list_.erase(list_.iterator_to(e));
    delete &e;
    return 1;
}

template <class>
void
headers::append (std::string const& field,
    std::string const& value)
{
    typename set_t::insert_commit_data d;
    auto const result (set_.insert_check (field, less{}, d));
    if (result.second)
    {
        element* const p = new element (field, value);
        list_.push_back (*p);
        set_.insert_commit (*p, d);
        return;
    }
    // If field already exists, append comma
    // separated value as per RFC2616 section 4.2
    auto& cur (result.first->data.second);
    cur.reserve (cur.size() + 1 + value.size());
    cur.append (1, ',');
    cur.append (value);
}

//------------------------------------------------------------------------------

template <class Streambuf>
void
write (Streambuf& stream, std::string const& s)
{
    stream.commit (boost::asio::buffer_copy (
        stream.prepare (s.size()), boost::asio::buffer(s)));
}

template <class Streambuf>
void
write (Streambuf& stream, char const* s)
{
    auto const len (::strlen(s));
    stream.commit (boost::asio::buffer_copy (
        stream.prepare (len), boost::asio::buffer (s, len)));
}

template <class Streambuf>
void
write (Streambuf& stream, headers const& h)
{
    for (auto const& _ : h)
    {
        write (stream, _.first);
        write (stream, ": ");
        write (stream, _.second);
        write (stream, "\r\n");
    }
}

template <class>
std::string
to_string (headers const& h)
{
    std::string s;
    std::size_t n (0);
    for (auto const& e : h)
        n += e.first.size() + 2 + e.second.size() + 2;
    s.reserve (n);
    for (auto const& e : h)
    {
        s.append (e.first);
        s.append (": ");
        s.append (e.second);
        s.append ("\r\n");
    }
    return s;
}

inline
std::ostream&
operator<< (std::ostream& s, headers const& h)
{
    s << to_string(h);
    return s;
}

template <class>
std::map <std::string, std::string>
build_map (headers const& h)
{
    std::map <std::string, std::string> c;
    for (auto const& e : h)
    {
        auto key (e.first);
        // TODO Replace with safe C++14 version
        std::transform (key.begin(), key.end(), key.begin(), ::tolower);
        c [key] = e.second;
    }
    return c;
}

} // http
} // beast

#endif
