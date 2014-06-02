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

#ifndef BEAST_HTTP_BASIC_URL_H_INCLUDED
#define BEAST_HTTP_BASIC_URL_H_INCLUDED

#include <boost/system/error_code.hpp>
#include <boost/utility/string_ref.hpp>

#include <beast/utility/noexcept.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace beast {
namespace http {

namespace detail {

class basic_url_base
{
public:
    typedef char value_type;
    typedef std::char_traits <value_type> traits_type;

    typedef boost::basic_string_ref <
        value_type, traits_type> string_ref;

    string_ref
    scheme () const noexcept
    {
        return m_scheme;
    }

    string_ref
    host () const noexcept
    {
        return m_host;
    }

    std::uint16_t
    port () const noexcept
    {
        return m_port;
    }

    string_ref
    port_string () const noexcept
    {
        return m_port_string;
    }

    string_ref
    path () const noexcept
    {
        return m_path;
    }

    string_ref
    query () const noexcept
    {
        return m_query;
    }

    string_ref
    fragment () const noexcept
    {
        return m_fragment;
    }

    string_ref
    userinfo () const noexcept
    {
        return m_userinfo;
    }

protected:
    void
    parse_impl (string_ref s, boost::system::error_code& ec);

    string_ref m_string_ref;
    string_ref m_scheme;
    string_ref m_host;
    std::uint16_t m_port;
    string_ref m_port_string;
    string_ref m_path;
    string_ref m_query;
    string_ref m_fragment;
    string_ref m_userinfo;
};

}

/** A URL. */
template <
    class Alloc = std::allocator <char>
>
class basic_url : public detail::basic_url_base
{
public:
    typedef std::basic_string <
        value_type, traits_type, Alloc> string_type;

    basic_url() = default;

    explicit basic_url (Alloc const& alloc)
        : m_string (alloc)
    {
    }

    void
    parse (string_ref s)
    {
        boost::system::error_code ec;
        parse (s, ec);
        if (ec)
            throw std::invalid_argument ("invalid url string");
    }

    boost::system::error_code
    parse (string_ref s,
        boost::system::error_code& ec)
    {
        parse_impl (s, ec);
        if (!ec)
        {
            m_string = string_type (s.begin(), s.end());
            m_string_ref = m_string;
        }
        return ec;
    }

    bool
    empty () const noexcept
    {
        return m_string.empty();
    }

    template <class Alloc1, class Alloc2>
    friend
    int
    compare (basic_url const& lhs,
             basic_url const& rhs) noexcept
    {
        return lhs.m_buf.compare (rhs.m_buf);
    }

private:
    string_type m_string;
};

using url = basic_url <std::allocator <char>>;

}
}

#endif
