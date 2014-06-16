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

#include <beast/http/URL.h>

namespace beast {

URL::URL ()
    : m_port (0)
{
}
URL::URL (
    String scheme_,
    String host_,
    std::uint16_t port_,
    String port_string_,
    String path_,
    String query_,
    String fragment_,
    String userinfo_)
    : m_scheme (scheme_)
    , m_host (host_)
    , m_port (port_)
    , m_port_string (port_string_)
    , m_path (path_)
    , m_query (query_)
    , m_fragment (fragment_)
    , m_userinfo (userinfo_)
{
}

URL::URL (URL const& other)
    : m_scheme (other.m_scheme)
    , m_host (other.m_host)
    , m_port (other.m_port)
    , m_port_string (other.m_port_string)
    , m_path (other.m_path)
    , m_query (other.m_query)
    , m_fragment (other.m_fragment)
    , m_userinfo (other.m_userinfo)
{
}

URL& URL::operator= (URL const& other)
{
    m_scheme = other.m_scheme;
    m_host = other.m_host;
    m_port = other.m_port;
    m_port_string = other.m_port_string;
    m_path = other.m_path;
    m_query = other.m_query;
    m_fragment = other.m_fragment;
    m_userinfo = other.m_userinfo;
    return *this;
}

//------------------------------------------------------------------------------

bool URL::empty () const
{
    return m_scheme == String::empty;
}

String URL::scheme () const
{
    return m_scheme;
}

String URL::host () const
{
    return m_host;
}

String URL::port_string () const
{
    return m_port_string;
}

std::uint16_t URL::port () const
{
    return m_port;
}

String URL::path () const
{
    return m_path;
}

String URL::query () const
{
    return m_query;
}

String URL::fragment () const
{
    return m_fragment;
}

String URL::userinfo () const
{
    return m_userinfo;
}

//------------------------------------------------------------------------------
/*
    From
        http://en.wikipedia.org/wiki/URI_scheme

    <scheme name> : <hierarchical part> [ ? <query> ] [ # <fragment> ]

    e.g.

    foo://username:password@example.com:8042/over/there/index.dtb?type=animal&name=narwhal#nose
*/
String URL::toString () const
{
    String s;

    s = scheme () + "://";
    
    if (userinfo () != String::empty)
        s = userinfo () + "@";

    s = s + host ();

    if (port () != 0)
        s = s + ":" + String::fromNumber (port ());

    s = s + path ();

    if (query () != String::empty)
        s = "?" + query ();

    if (fragment () != String::empty)
        s = "#" + fragment ();

    return s;
}

std::string URL::to_string() const
{
    return toString().toStdString();
}

std::ostream& operator<< (std::ostream &os, URL const& url)
{
    os << url.to_string();
    return os;
}

//------------------------------------------------------------------------------

std::size_t hash_value (URL const& v)
{
    return std::size_t (v.toString().hash());
}

}


// boost::hash support