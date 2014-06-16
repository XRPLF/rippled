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

#ifndef RIPPLE_SITEFILES_SECTION_H_INCLUDED
#define RIPPLE_SITEFILES_SECTION_H_INCLUDED

#include <ripple/common/UnorderedContainers.h>
#include <vector>

namespace ripple {
namespace SiteFiles {

/** A Site File section.
    Each section has a name, an associative map of key/value pairs,
    and a vector of zero or more free-form data strings.
*/
class Section
{
public:
    typedef ripple::unordered_map <std::string, std::string> MapType;
    typedef std::vector <std::string> DataType;

    Section(int = 0); // dummy argument for emplace()

    // Observers
    std::string const& get (std::string const& key) const;
    std::string const& operator[] (std::string const& key) const;
    DataType const& data() const;

    // Modifiers
    void set (std::string const& key, std::string const& value);
    std::string& operator[] (std::string const& key);
    void push_back (std::string const& data);

private:
    MapType m_map;
    DataType m_data;
};

}
}

#endif
