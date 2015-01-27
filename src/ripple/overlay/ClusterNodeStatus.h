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

namespace ripple {

class ClusterNodeStatus
{
public:

    ClusterNodeStatus() : mLoadLevel(0), mReportTime(0)
    { ; }

    explicit ClusterNodeStatus(std::string const& name) : mNodeName(name), mLoadLevel(0), mReportTime(0)
    { ; }

    ClusterNodeStatus(std::string const& name, std::uint32_t level, std::uint32_t rtime) :
        mNodeName(name),
        mLoadLevel(level),
        mReportTime(rtime)
    { ; }

    std::string const& getName()
    {
        return mNodeName;
    }

    std::uint32_t getLoadLevel()
    {
        return mLoadLevel;
    }

    std::uint32_t getReportTime()
    {
        return mReportTime;
    }

    bool update(ClusterNodeStatus const& status)
    {
        if (status.mReportTime <= mReportTime)
            return false;
        mLoadLevel = status.mLoadLevel;
        mReportTime = status.mReportTime;
        if (mNodeName.empty() || !status.mNodeName.empty())
            mNodeName = status.mNodeName;
        return true;
    }

private:
    std::string       mNodeName;
    std::uint32_t     mLoadLevel;
    std::uint32_t     mReportTime;
};

} // ripple

#endif
