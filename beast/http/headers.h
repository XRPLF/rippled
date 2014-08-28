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

#include <beast/utility/ci_char_traits.h>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <ostream>
#include <string>
#include <utility>

namespace beast {
namespace http {

namespace detail {

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

    std::string field;
    std::string value;
};

struct less : private beast::ci_less
{
    template <class String>
    bool
    operator() (String const& lhs, element const& rhs) const;

    template <class String>
    bool
    operator() (element const& lhs, String const& rhs) const;
};

} // detail

/** Holds a collection of HTTP headers. */
class headers : private detail::less
{
private:
    typedef boost::intrusive::make_list <detail::element,
        boost::intrusive::constant_time_size <false>
            >::type list_t;

    typedef boost::intrusive::make_set <detail::element,
        boost::intrusive::constant_time_size <true>
            >::type set_t;

    list_t list_;
    set_t set_;

public:
    typedef list_t::const_iterator iterator;
    typedef iterator const_iterator;

    ~headers()
    {
        clear();
    }

    headers() = default;

#if defined(_MSC_VER) && _MSC_VER <= 1800
    headers (headers&& other);
    headers& operator= (headers&& other);

#else
    headers (headers&& other) = default;
    headers& operator= (headers&& other) = default;

#endif

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

namespace detail {

template <class>
element::element (
    std::string const& f, std::string const& v)
    : field (f)
    , value (v)
{
}

template <class String>
bool
less::operator() (
    String const& lhs, element const& rhs) const
{
    return beast::ci_less::operator() (lhs, rhs.field);
}

template <class String>
bool
less::operator() (
    element const& lhs, String const& rhs) const
{
    return beast::ci_less::operator() (lhs.field, rhs);
}

} // detail

//------------------------------------------------------------------------------

#if defined(_MSC_VER) && _MSC_VER <= 1800
inline
headers::headers (headers&& other)
    : list_ (std::move(other.list_))
    , set_ (std::move(other.set_))
{

}

inline
headers&
headers::operator= (headers&& other)
{
    list_ = std::move(other.list_);
    set_ = std::move(other.set_);
    return *this;
}
#endif

inline
headers::headers (headers const& other)
{
    for (auto const& e : other.list_)
        append (e.field, e.value);
}

inline
headers&
headers::operator= (headers const& other)
{
    clear();
    for (auto const& e : other.list_)
        append (e.field, e.value);
    return *this;
}

inline
headers::iterator
headers::begin() const
{
    return list_.cbegin();
}

inline
headers::iterator
headers::end() const
{
    return list_.cend();
}

inline
headers::iterator
headers::cbegin() const
{
    return list_.cbegin();
}

inline
headers::iterator
headers::cend() const
{
    return list_.cend();
}

template <class>
headers::iterator
headers::find (std::string const& field) const
{
    auto const iter (set_.find (field,
        std::cref(static_cast<less const&>(*this))));
    if (iter == set_.end())
        return list_.end();
    return list_.iterator_to (*iter);
}

template <class>
std::string const&
headers::operator[] (std::string const& field) const
{
    static std::string none;
    auto const found (find (field));
    if (found == end())
        return none;
    return found->value;
}

template <class>
void
headers::clear() noexcept
{
    for (auto iter (list_.begin()); iter != list_.end();)
        delete &(*iter++);
}

template <class>
void
headers::append (std::string const& field,
    std::string const& value)
{
    set_t::insert_commit_data d;
    auto const result (set_.insert_check (field,
        std::cref(static_cast<less const&>(*this)), d));
    if (result.second)
    {
        detail::element* const p =
            new detail::element (field, value);
        list_.push_back (*p);
        set_.insert_commit (*p, d);
        return;
    }
    // If field already exists, append comma
    // separated value as per RFC2616 section 4.2
    auto& cur (result.first->value);
    cur.reserve (cur.size() + 1 + value.size());
    cur.append (1, ',');
    cur.append (value);
}

//------------------------------------------------------------------------------

template <class>
std::string
to_string (headers const& h)
{
    std::string s;
    std::size_t n (0);
    for (auto const& e : h)
        n += e.field.size() + 2 + e.value.size() + 2;
    s.reserve (n);
    for (auto const& e : h)
    {
        s.append (e.field);
        s.append (": ");
        s.append (e.value);
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
        auto key (e.field);
        // TODO Replace with safe C++14 version
        std::transform (key.begin(), key.end(), key.begin(), ::tolower);
        c [key] = e.value;
    }
    return c;
}

} // http
} // beast

#endif