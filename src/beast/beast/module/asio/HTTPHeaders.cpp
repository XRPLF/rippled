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

#include <beast/module/asio/HTTPHeaders.h>
#include <algorithm>

namespace beast {

HTTPHeaders::HTTPHeaders ()
{
}

HTTPHeaders::HTTPHeaders (StringPairArray& fields)
{
    m_fields.swapWith (fields);
}

HTTPHeaders::HTTPHeaders (StringPairArray const& fields)
    : m_fields (fields)
{
}

HTTPHeaders::HTTPHeaders (HTTPHeaders const& other)
    : m_fields (other.m_fields)
{
}

HTTPHeaders& HTTPHeaders::operator= (HTTPHeaders const& other)
{
    m_fields = other.m_fields;
    return *this;
}

bool HTTPHeaders::empty () const
{
    return m_fields.size () == 0;
}

std::size_t HTTPHeaders::size () const
{
    return m_fields.size ();
}

HTTPField HTTPHeaders::at (int index) const
{
    return HTTPField (m_fields.getAllKeys () [index],
                      m_fields.getAllValues () [index]);
}

HTTPField HTTPHeaders::operator[] (int index) const
{
    return at (index);
}

String HTTPHeaders::get (String const& field) const
{
    return m_fields [field];
}

String HTTPHeaders::operator[] (String const& field) const
{
    return get (field);
}

String HTTPHeaders::toString () const
{
    String s;
    for (int i = 0; i < m_fields.size (); ++i)
    {
        HTTPField const field (at(i));
        s << field.name() << ": " << field.value() << newLine;
    }
    return s;
}

std::map <std::string, std::string>
HTTPHeaders::build_map() const
{
    std::map <std::string, std::string> c;
    auto const& k (m_fields.getAllKeys());
    auto const& v (m_fields.getAllValues());
    for (std::size_t i = 0; i < m_fields.size(); ++i)
    {
        auto key (k[i].toStdString());
        auto const value (v[i].toStdString());
        std::transform (key.begin(), key.end(), key.begin(), ::tolower);
        c[key] = value;
    }
    return c;
}

}
