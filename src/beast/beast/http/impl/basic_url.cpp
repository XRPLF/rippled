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

#include <beast/http/basic_url.h>

#include <beast/http/impl/joyent_parser.h>

namespace beast {
namespace http {
namespace detail {

void
basic_url_base::parse_impl (string_ref s, boost::system::error_code& ec)
{
    joyent::http_parser_url p;

    value_type const* const data (s.data());
    
    int const error (joyent::http_parser_parse_url (
        data, s.size(), false, &p));

    if (error)
    {
        ec = boost::system::error_code (
            boost::system::errc::invalid_argument,
            boost::system::generic_category());
        return;
    }

    if ((p.field_set & (1<<joyent::UF_SCHEMA)) != 0)
    {
        m_scheme = string_ref (
            data + p.field_data [joyent::UF_SCHEMA].off,
                p.field_data [joyent::UF_SCHEMA].len);
    }
    else
    {
        m_scheme = string_ref {};
    }

    if ((p.field_set & (1<<joyent::UF_HOST)) != 0)
    {
        m_host = string_ref (
            data + p.field_data [joyent::UF_HOST].off,
                p.field_data [joyent::UF_HOST].len);
    }
    else
    {
        m_host = string_ref {};
    }

    if ((p.field_set & (1<<joyent::UF_PORT)) != 0)
    {
        m_port = p.port;
        m_port_string = string_ref (
            data + p.field_data [joyent::UF_PORT].off,
                p.field_data [joyent::UF_PORT].len);
    }
    else
    {
        m_port = 0;
        m_port_string = string_ref {};
    }

    if ((p.field_set & (1<<joyent::UF_PATH)) != 0)
    {
        m_path = string_ref (
            data + p.field_data [joyent::UF_PATH].off,
                p.field_data [joyent::UF_PATH].len);
    }
    else
    {
        m_path = string_ref {};
    }

    if ((p.field_set & (1<<joyent::UF_QUERY)) != 0)
    {
        m_query = string_ref (
            data + p.field_data [joyent::UF_QUERY].off,
                p.field_data [joyent::UF_QUERY].len);
    }
    else
    {
        m_query = string_ref {};
    }

    if ((p.field_set & (1<<joyent::UF_FRAGMENT)) != 0)
    {
        m_fragment = string_ref (
            data + p.field_data [joyent::UF_FRAGMENT].off,
                p.field_data [joyent::UF_FRAGMENT].len);
    }
    else
    {
        m_fragment = string_ref {};
    }

    if ((p.field_set & (1<<joyent::UF_USERINFO)) != 0)
    {
        m_userinfo = string_ref (
            data + p.field_data [joyent::UF_USERINFO].off,
                p.field_data [joyent::UF_USERINFO].len);
    }
    else
    {
        m_userinfo = string_ref {};
    }
}

} // detail
} // http
} // beast
