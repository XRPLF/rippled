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

#include <ripple/peerfinder/impl/SourceStrings.h>

namespace ripple {
namespace PeerFinder {

class SourceStringsImp : public SourceStrings
{
public:
    SourceStringsImp (std::string const& name, Strings const& strings)
        : m_name (name)
        , m_strings (strings)
    {
    }

    ~SourceStringsImp () override
    {
    }

    std::string const& name () override
    {
        return m_name;
    }

    void fetch (Results& results, beast::Journal journal) override
    {
        results.addresses.resize (0);
        results.addresses.reserve (m_strings.size());
        for (int i = 0; i < m_strings.size (); ++i)
        {
            beast::IP::Endpoint ep (beast::IP::Endpoint::from_string (m_strings [i]));
            if (is_unspecified (ep))
                ep = beast::IP::Endpoint::from_string_altform (m_strings [i]);
            if (! is_unspecified (ep))
                results.addresses.push_back (ep);
        }
    }

private:
    std::string m_name;
    Strings m_strings;
};

//------------------------------------------------------------------------------

beast::SharedPtr <Source>
SourceStrings::New (std::string const& name, Strings const& strings)
{
    return new SourceStringsImp (name, strings);
}

}
}
