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

URL::URL (
    std::string scheme_,
    std::string host_,
    std::uint16_t port_,
    std::string port_string_,
    std::string path_,
    std::string query_,
    std::string fragment_,
    std::string userinfo_)
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

//------------------------------------------------------------------------------

bool
URL::empty () const
{
    return m_scheme.empty ();
}

std::string
const& URL::scheme () const
{
    return m_scheme;
}

std::string
const& URL::host () const
{
    return m_host;
}

std::string
const& URL::port_string () const
{
    return m_port_string;
}

std::uint16_t
URL::port () const
{
    return m_port;
}

std::string
const& URL::path () const
{
    return m_path;
}

std::string
const& URL::query () const
{
    return m_query;
}

std::string
const& URL::fragment () const
{
    return m_fragment;
}

std::string
const& URL::userinfo () const
{
    return m_userinfo;
}

//------------------------------------------------------------------------------
std::pair<bool, URL>
parse_URL(std::string const& url)
{
    std::size_t const buflen (url.size ());
    char const* const buf (url.c_str ());

    joyent::http_parser_url parser;

    if (joyent::http_parser_parse_url (buf, buflen, false, &parser) != 0)
        return std::make_pair (false, URL{});

    std::string scheme;
    std::string host;
    std::uint16_t port (0);
    std::string port_string;
    std::string path;
    std::string query;
    std::string fragment;
    std::string userinfo;

    if ((parser.field_set & (1<<joyent::UF_SCHEMA)) != 0)
    {
        scheme = std::string (
            buf + parser.field_data [joyent::UF_SCHEMA].off,
                parser.field_data [joyent::UF_SCHEMA].len);
    }

    if ((parser.field_set & (1<<joyent::UF_HOST)) != 0)
    {
        host = std::string (
            buf + parser.field_data [joyent::UF_HOST].off,
                parser.field_data [joyent::UF_HOST].len);
    }

    if ((parser.field_set & (1<<joyent::UF_PORT)) != 0)
    {
        port = parser.port;
        port_string = std::string (
            buf + parser.field_data [joyent::UF_PORT].off,
                parser.field_data [joyent::UF_PORT].len);
    }

    if ((parser.field_set & (1<<joyent::UF_PATH)) != 0)
    {
        path = std::string (
            buf + parser.field_data [joyent::UF_PATH].off,
                parser.field_data [joyent::UF_PATH].len);
    }

    if ((parser.field_set & (1<<joyent::UF_QUERY)) != 0)
    {
        query = std::string (
            buf + parser.field_data [joyent::UF_QUERY].off,
                parser.field_data [joyent::UF_QUERY].len);
    }

    if ((parser.field_set & (1<<joyent::UF_FRAGMENT)) != 0)
    {
        fragment = std::string (
            buf + parser.field_data [joyent::UF_FRAGMENT].off,
                parser.field_data [joyent::UF_FRAGMENT].len);
    }

    if ((parser.field_set & (1<<joyent::UF_USERINFO)) != 0)
    {
        userinfo = std::string (
            buf + parser.field_data [joyent::UF_USERINFO].off,
                parser.field_data [joyent::UF_USERINFO].len);
    }

    return std::make_pair (true,
        URL {scheme, host, port, port_string, path, query, fragment, userinfo});
}

std::string
to_string (URL const& url)
{
    std::string s;

    if (!url.empty ())
    {
        // Pre-allocate enough for components and inter-component separators
        s.reserve (
            url.scheme ().length () + url.userinfo ().length () +
            url.host ().length () + url.port_string ().length () +
            url.query ().length () + url.fragment ().length () + 16);

        s.append (url.scheme ());
        s.append ("://");

        if (!url.userinfo ().empty ())
        {
            s.append (url.userinfo ());
            s.append ("@");
        }

        s.append (url.host ());

        if (url.port ())
        {
            s.append (":");
            s.append (url.port_string ());
        }

        s.append (url.path ());

        if (!url.query ().empty ())
        {
            s.append ("?");
            s.append (url.query ());
        }

        if (!url.fragment ().empty ())
        {
            s.append ("#");
            s.append (url.fragment ());
        }
    }

    return s;
}

std::ostream&
operator<< (std::ostream &os, URL const& url)
{
    os << to_string (url);
    return os;
}

}
