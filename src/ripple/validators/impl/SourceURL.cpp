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

#include <memory>

namespace ripple {
namespace Validators {

class SourceURLImp
    : public SourceURL
    , public beast::LeakChecked <SourceURLImp>
{
public:
    explicit SourceURLImp (beast::URL const& url)
        : m_url (url)
        , m_client (beast::asio::HTTPClientBase::New ())
    {
    }

    ~SourceURLImp ()
    {
    }

    std::string to_string () const
    {
        std::stringstream ss;
        ss <<
            "URL: '" << m_url.to_string() << "'";
        return ss.str();
    }

    beast::String uniqueID () const
    {
        return "URL," + m_url.toString();
    }

    beast::String createParam ()
    {
        return m_url.toString();
    }

    void cancel ()
    {
        m_client->cancel ();
    }

    void fetch (Results& results, beast::Journal journal)
    {
        auto httpResult (m_client->get (m_url));

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
    beast::URL m_url;
    std::unique_ptr <beast::asio::HTTPClientBase> m_client;
};

//------------------------------------------------------------------------------

SourceURL* SourceURL::New (
    beast::URL const& url)
{
    return new SourceURLImp (url);
}

}
}
