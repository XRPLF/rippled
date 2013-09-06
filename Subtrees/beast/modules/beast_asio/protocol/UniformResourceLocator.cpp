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

UniformResourceLocator::UniformResourceLocator ()
    : m_port (0)
{
}

UniformResourceLocator::UniformResourceLocator (
    String scheme_,
    String host_,
    uint16 port_,
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

UniformResourceLocator::UniformResourceLocator (UniformResourceLocator const& other)
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

UniformResourceLocator& UniformResourceLocator::operator= (UniformResourceLocator const& other)
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

String UniformResourceLocator::scheme () const
{
    return m_scheme;
}

String UniformResourceLocator::host () const
{
    return m_host;
}

String UniformResourceLocator::port_string () const
{
    return m_port_string;
}

uint16 UniformResourceLocator::port () const
{
    return m_port;
}

String UniformResourceLocator::path () const
{
    return m_path;
}

String UniformResourceLocator::query () const
{
    return m_query;
}

String UniformResourceLocator::fragment () const
{
    return m_fragment;
}

String UniformResourceLocator::userinfo () const
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
String UniformResourceLocator::full () const
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
//------------------------------------------------------------------------------

ParsedURL::ParsedURL ()
    : m_error (0)
{
}

ParsedURL::ParsedURL (String const& url)
{
    std::string const ss (url.toStdString ());
    std::size_t const buflen (ss.size ());
    char const* const buf (ss.c_str ());

    http_parser_url u;

    m_error = http_parser_parse_url (buf, buflen, false, &u);

    String scheme_;
    String host_;
    uint16 port_ (0);
    String port_string_;
    String path_;
    String query_;
    String fragment_;
    String userinfo_;

    if (m_error == 0)
    {
        if ((u.field_set & (1<<UF_SCHEMA)) != 0)
        {
            scheme_ = String (
                buf + u.field_data [UF_SCHEMA].off,
                    u.field_data [UF_SCHEMA].len);
        }

        if ((u.field_set & (1<<UF_HOST)) != 0)
        {
            host_ = String (
                buf + u.field_data [UF_HOST].off,
                    u.field_data [UF_HOST].len);
        }

        if ((u.field_set & (1<<UF_PORT)) != 0)
        {
            port_ = u.port;
            port_string_ = String (
                buf + u.field_data [UF_PORT].off,
                    u.field_data [UF_PORT].len);
        }
        else
        {
            port_ = 0;
        }

        if ((u.field_set & (1<<UF_PATH)) != 0)
        {
            path_ = String (
                buf + u.field_data [UF_PATH].off,
                    u.field_data [UF_PATH].len);
        }

        if ((u.field_set & (1<<UF_QUERY)) != 0)
        {
            query_ = String (
                buf + u.field_data [UF_QUERY].off,
                    u.field_data [UF_QUERY].len);
        }

        if ((u.field_set & (1<<UF_FRAGMENT)) != 0)
        {
            fragment_ = String (
                buf + u.field_data [UF_FRAGMENT].off,
                    u.field_data [UF_FRAGMENT].len);
        }

        if ((u.field_set & (1<<UF_USERINFO)) != 0)
        {
            userinfo_ = String (
                buf + u.field_data [UF_USERINFO].off,
                    u.field_data [UF_USERINFO].len);
        }

        m_url = UniformResourceLocator (
            scheme_,
            host_,
            port_,
            port_string_,
            path_,
            query_,
            fragment_,
            userinfo_);
    }
}

ParsedURL::ParsedURL (int error, UniformResourceLocator const& url)
    : m_error (error)
    , m_url (url)
{
}

ParsedURL::ParsedURL (ParsedURL const& other)
    : m_error (other.m_error)
    , m_url (other.m_url)
{
}

ParsedURL& ParsedURL::operator= (ParsedURL const& other)
{
    m_error = other.m_error;
    m_url = other.m_url;
    return *this;
}

int ParsedURL::error () const
{
    return m_error;
}

UniformResourceLocator ParsedURL::url () const
{
    return m_url;
}

//------------------------------------------------------------------------------

class ParsedURLTests : public UnitTest
{
public:
    void checkURL (String const& url)
    {
        ParsedURL result (url);
        expect (result.error () == 0);
        expect (result.url ().full () == url);
    }

    void testURL ()
    {
        beginTestCase ("parse URL");

        checkURL ("http://www.boost.org/doc/libs/1_54_0/doc/html/boost_asio/reference.html");
    }

    void runTest ()
    {
        testURL ();
    }

    ParsedURLTests () : UnitTest ("ParsedURL", "beast", runManual)
    {
    }
};

static ParsedURLTests parsedURLTests;
