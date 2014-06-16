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

#ifndef RIPPLE_NET_RPC_RPCSUB_H_INCLUDED
#define RIPPLE_NET_RPC_RPCSUB_H_INCLUDED

namespace ripple {

/** Subscription object for JSON RPC. */
class RPCSub : public InfoSub
{
public:
    typedef std::shared_ptr <RPCSub> pointer;
    typedef pointer const& ref;

    static pointer New (InfoSub::Source& source,
        boost::asio::io_service& io_service, JobQueue& jobQueue,
            const std::string& strUrl, const std::string& strUsername,
            const std::string& strPassword);

    virtual void setUsername (const std::string& strUsername) = 0;
    virtual void setPassword (const std::string& strPassword) = 0;

protected:
    explicit RPCSub (InfoSub::Source& source);
};

} // ripple

#endif
