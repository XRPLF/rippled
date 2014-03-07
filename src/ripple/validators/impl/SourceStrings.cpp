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
namespace Validators {

class SourceStringsImp
    : public SourceStrings
    , public beast::LeakChecked <SourceStringsImp>
{
public:
    SourceStringsImp (
        beast::String name, beast::StringArray const& strings)
        : m_name (name)
        , m_strings (strings)
    {
    }

    ~SourceStringsImp ()
    {
    }

    std::string to_string () const
    {
        return m_name.toStdString();
    }

    beast::String uniqueID () const
    {
        // VFALCO TODO This can't be right...?
        return beast::String::empty;
    }

    beast::String createParam ()
    {
        return beast::String::empty;
    }

    void fetch (Results& results, beast::Journal journal)
    {
        results.list.reserve (m_strings.size ());

        for (int i = 0; i < m_strings.size (); ++i)
        {
            std::string const s (m_strings [i].toStdString ());
            Utilities::parseResultLine (results, s);
        }

        results.success = results.list.size () > 0;
        results.expirationTime = beast::Time::getCurrentTime () +
                                 beast::RelativeTime::hours (24);
    }

private:
    beast::String m_name;
    beast::StringArray m_strings;
};

//------------------------------------------------------------------------------

SourceStrings* SourceStrings::New (
    beast::String name, beast::StringArray const& strings)
{
    return new SourceStringsImp (name, strings);
}

}
}
