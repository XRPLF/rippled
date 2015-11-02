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

#ifndef RIPPLE_APP_PEERS_CLUSTERNODESTATUS_H_INCLUDED
#define RIPPLE_APP_PEERS_CLUSTERNODESTATUS_H_INCLUDED

#include <ripple/protocol/RippleAddress.h>
#include <cstdint>
#include <string>

namespace ripple {

class ClusterNode
{
public:
    ClusterNode() = delete;

    ClusterNode(
            RippleAddress const& identity,
            std::string const& name,
            std::uint32_t fee = 0,
            std::uint32_t rtime = 0)
        : identity_ (identity)
        , name_(name)
        , mLoadFee(fee)
        , mReportTime(rtime)
    { }

    std::string const& name() const
    {
        return name_;
    }

    std::uint32_t getLoadFee() const
    {
        return mLoadFee;
    }

    std::uint32_t getReportTime() const
    {
        return mReportTime;
    }

    RippleAddress const&
    identity () const
    {
        return identity_;
    }

private:
    RippleAddress identity_;
    std::string name_;
    std::uint32_t mLoadFee = 0;
    std::uint32_t mReportTime = 0;
};

} // ripple

#endif
