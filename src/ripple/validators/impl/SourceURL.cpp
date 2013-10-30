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

class SourceURLImp
    : public SourceURL
    , public LeakChecked <SourceURLImp>
{
public:
    explicit SourceURLImp (URL const& url)
        : m_url (url)
        , m_client (HTTPClientBase::New ())
    {
    }

    ~SourceURLImp ()
    {
    }

    String name ()
    {
        return "URL: '" + m_url.toString() + "'";
    }

    String uniqueID ()
    {
        return "URL," + m_url.toString();
    }

    String createParam ()
    {
        return m_url.toString();
    }

    void cancel ()
    {
        m_client->cancel ();
    }

    void fetch (Results& results, Journal journal)
    {
        HTTPClientBase::result_type httpResult (m_client->get (m_url));

        if (httpResult.first == 0)
        {
            Utilities::ParseResultLine lineFunction (results, journal);
            std::string const s (httpResult.second->body().to_string());
            Utilities::processLines (s.begin(), s.end(), lineFunction);
        }
        else
        {
            journal.error <<
                "HTTP GET to " << m_url <<
                " failed: '" << httpResult.first.message () << "'";
        }
    }

private:
    URL m_url;
    ScopedPointer <HTTPClientBase> m_client;
};

//------------------------------------------------------------------------------

SourceURL* SourceURL::New (
    URL const& url)
{
    return new SourceURLImp (url);
}

}
}
