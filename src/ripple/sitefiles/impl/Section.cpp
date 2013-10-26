//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

namespace ripple {
namespace SiteFiles {

Section::Section(int)
{
}

std::string const& Section::get (std::string const& key) const
{
    MapType::const_iterator iter (m_map.find (key));
    if (iter != m_map.end())
        return iter->second;
    static std::string const none;
    return none;
}

std::string const& Section::operator[] (std::string const& key) const
{
    return get (key);
}

std::vector <std::string> const& Section::data() const
{
    return m_data;
}

void Section::set (std::string const& key, std::string const& value)
{
    m_map [key] = value;
}

std::string& Section::operator[] (std::string const& key)
{
    return m_map [key];
}

void Section::push_back (std::string const& data)
{
    m_data.push_back (data);
}

}
}
