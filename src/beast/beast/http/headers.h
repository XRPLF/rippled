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
#include <beast/utility/empty_base_optimization.h>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

namespace beast {
namespace http {

namespace detail {

template <class = void>
class basic_headers_helper
{
public:
    using value_type = std::pair<std::string, std::string>;

protected:
    struct element
        : boost::intrusive::set_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>>
        , boost::intrusive::list_base_hook <
            boost::intrusive::link_mode <
                boost::intrusive::normal_link>>
    {
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
    {
        using argument_type = element;
        using result_type = value_type;

        value_type const&
        operator() (element const& e) const
        {
            return e.data;
        }
    };

    using list_type = typename boost::intrusive::make_list <element,
        boost::intrusive::constant_time_size <false>>::type;

    using set_type = typename boost::intrusive::make_set <element,
        boost::intrusive::constant_time_size <true>>::type;

public:
    // VFALCO Shouldn't be public but would cause a
    //        nightmare in the base list of basic_headers
    template <class Alloc>
    using make_alloc = typename std::allocator_traits<
        Alloc>::template rebind_alloc<element>;

    using iterator = typename boost::transform_iterator <
        transform, typename list_type::const_iterator>;

    using const_iterator = iterator;
};

} // detail

/** Holds a collection of HTTP headers */
template <class Alloc>
class basic_headers
    : public detail::basic_headers_helper<>
    , private empty_base_optimization<
        typename detail::basic_headers_helper<
            >::make_alloc<Alloc>>
{
public:
    using allocator_type = make_alloc<Alloc>;

private:
    using alloc_traits =
        typename std::allocator_traits<
            allocator_type>;

    list_type list_;
    set_type set_;

public:
    ~basic_headers();

    basic_headers (Alloc alloc = Alloc{});

    basic_headers (basic_headers&& other);
    basic_headers& operator= (basic_headers&& other);

    template <class OtherAlloc>
    basic_headers (basic_headers<OtherAlloc> const& other);

    template <class OtherAlloc>
    basic_headers& operator= (basic_headers<OtherAlloc> const& other);

    /** Returns an iterator to field/value pairs in order of appearance. */
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

    /** Returns an iterator to the case-insensitive matching header. */
    iterator
    find (std::string const& field) const;

    /** Returns the value for a case-insensitive matching header, or "" */
    std::string const&
    operator[] (std::string const& field) const;

    /** Clear the contents of the basic_headers<Alloc>::. */
    void
    clear() noexcept;

    /** Remove a field.
        @return The number of fields removed.
    */
    std::size_t
    erase (std::string const& field);

    /** Append a field value.
        If a field value already exists the new value will be
        extended as per RFC2616 Section 4.2.
    */
    // VFALCO TODO Consider allowing rvalue references for std::move
    void
    append (std::string const& field, std::string const& value);
};

template <class Alloc>
std::string
to_string (basic_headers<Alloc> const& h);

// HACK!
template <class Alloc>
std::map <std::string, std::string>
build_map (basic_headers<Alloc> const& h);

//------------------------------------------------------------------------------

namespace detail {

template <class _>
basic_headers_helper<_>::element::element (
    std::string const& f, std::string const& v)
{
    data.first = f;
    data.second = v;
}

template <class _>
template <class String>
bool
basic_headers_helper<_>::less::operator() (
    String const& lhs, element const& rhs) const
{
    return beast::ci_less::operator()(lhs, rhs.data.first);
}

template <class _>
template <class String>
bool
basic_headers_helper<_>::less::operator() (
    element const& lhs, String const& rhs) const
{
    return beast::ci_less::operator()(lhs.data.first, rhs);
}

} // detail

//------------------------------------------------------------------------------

template <class Alloc>
basic_headers<Alloc>::~basic_headers()
{
    clear();
}

template <class Alloc>
basic_headers<Alloc>::basic_headers(Alloc alloc)
    : empty_base_optimization<allocator_type>(alloc)
{
}

template <class Alloc>
basic_headers<Alloc>::basic_headers (basic_headers<Alloc>&& other)
    : list_(std::move(other.list_))
    , set_(std::move(other.set_))
{
    other.list_.clear();
    other.set_.clear();
}

template <class Alloc>
basic_headers<Alloc>&
basic_headers<Alloc>::operator=(basic_headers<Alloc>&& other)
{
    clear();
    list_ = std::move(other.list_);
    set_ = std::move(other.set_);
    other.list_.clear();
    other.set_.clear();
    return *this;
}

template <class Alloc>
template <class OtherAlloc>
basic_headers<Alloc>::basic_headers(
    basic_headers<OtherAlloc> const& other)
{
    for (auto const& e : other.list_)
        append (e.data.first, e.data.second);
}

template <class Alloc>
template <class OtherAlloc>
basic_headers<Alloc>&
basic_headers<Alloc>::operator= (basic_headers<OtherAlloc> const& other)
{
    clear();
    for (auto const& e : other.list_)
        append (e.data.first, e.data.second);
    return *this;
}

template <class Alloc>
auto
basic_headers<Alloc>::find (std::string const& field) const ->
    iterator
{
    auto const iter (set_.find (field, less{}));
    if (iter == set_.end())
        return {list_.end(), transform{}};
    return {list_.iterator_to (*iter), transform{}};
}

template <class Alloc>
std::string const&
basic_headers<Alloc>::operator[] (std::string const& field) const
{
    static std::string none;
    auto const found (find (field));
    if (found == end())
        return none;
    return found->second;
}

template <class Alloc>
void
basic_headers<Alloc>::clear() noexcept
{
    for (auto iter (list_.begin()); iter != list_.end();)
    {
        element& e = *iter++;
        alloc_traits::destroy(
            this->member(), &e);
        alloc_traits::deallocate(
            this->member(), &e, 1);
    }
    list_.clear();
}

template <class Alloc>
std::size_t
basic_headers<Alloc>::erase (std::string const& field)
{
    auto const iter = set_.find(field, less{});
    if (iter == set_.end())
        return 0;
    element& e = *iter;
    set_.erase(set_.iterator_to(e));
    list_.erase(list_.iterator_to(e));
    alloc_traits::destroy(
        this->member(), &e);
    alloc_traits::deallocate(
        this->member(), &e, 1);
    return 1;
}

template <class Alloc>
void
basic_headers<Alloc>::append (std::string const& field,
    std::string const& value)
{
    set_type::insert_commit_data d;
    auto const result (
        set_.insert_check (field, less{}, d));
    if (result.second)
    {
        element& e = *alloc_traits::allocate(
            this->member(), 1);
        alloc_traits::construct(
            this->member(), &e, field, value);
        list_.push_back (e);
        set_.insert_commit (e, d);
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

template <class Streambuf, class Alloc>
void
write (Streambuf& stream, basic_headers<Alloc> const& h)
{
    for (auto const& _ : h)
    {
        write (stream, _.first);
        write (stream, ": ");
        write (stream, _.second);
        write (stream, "\r\n");
    }
}

template <class Alloc>
std::string
to_string (basic_headers<Alloc> const& h)
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

template <class Alloc>
std::ostream&
operator<< (std::ostream& s, basic_headers<Alloc> const& h)
{
    s << to_string(h);
    return s;
}

template <class Alloc>
std::map <std::string, std::string>
build_map (basic_headers<Alloc> const& h)
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

using headers = basic_headers<std::allocator<char>>;

} // http
} // beast

#endif
