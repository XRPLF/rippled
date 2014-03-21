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

#ifndef RIPPLE_WSDOOR_H_INCLUDED
#define RIPPLE_WSDOOR_H_INCLUDED

namespace ripple {

/** Handles accepting incoming WebSocket connections. */
class WSDoor : public beast::Stoppable
{
protected:
    explicit WSDoor (Stoppable& parent);

public:
    virtual ~WSDoor () { }

    static WSDoor* New (Resource::Manager& resourceManager,
        InfoSub::Source& source, std::string const& strIp,
            int iPort, bool bPublic, bool bProxy, boost::asio::ssl::context& ssl_context);
};

} // ripple

#endif
