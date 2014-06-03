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

#ifndef RIPPLE_CLUSTERNODESTATUS_H_INCLUDED
#define RIPPLE_CLUSTERNODESTATUS_H_INCLUDED

namespace ripple {

class ClusterNodeStatus
{
public:

    ClusterNodeStatus() : mLoadFee(0), mReportTime(0)
    { ; }

    explicit ClusterNodeStatus(std::string const& name) : mNodeName(name), mLoadFee(0), mReportTime(0)
    { ; }

    ClusterNodeStatus(const std::string& name, std::uint32_t fee, std::uint32_t rtime) :
        mNodeName(name),
        mLoadFee(fee),
        mReportTime(rtime)
    { ; }

    std::string const& getName()
    {
        return mNodeName;
    }

    std::uint32_t getLoadFee()
    {
        return mLoadFee;
    }

    std::uint32_t getReportTime()
    {
        return mReportTime;
    }

    bool update(ClusterNodeStatus const& status)
    {
        if (status.mReportTime <= mReportTime)
            return false;
        mLoadFee = status.mLoadFee;
        mReportTime = status.mReportTime;
        if (mNodeName.empty() || !status.mNodeName.empty())
            mNodeName = status.mNodeName;
        return true;
    }

private:
    std::string       mNodeName;
    std::uint32_t     mLoadFee;
    std::uint32_t     mReportTime;
};

} // ripple

#endif
