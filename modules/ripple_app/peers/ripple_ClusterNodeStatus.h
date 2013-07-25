//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_CLUSTERNODESTATUS_H_INCLUDED
#define RIPPLE_CLUSTERNODESTATUS_H_INCLUDED

class ClusterNodeStatus
{
public:

    ClusterNodeStatus() : mLoadFee(0), mReportTime(0)
    { ; }

    explicit ClusterNodeStatus(std::string const& name) : mNodeName(name), mLoadFee(0), mReportTime(0)
    { ; }

    ClusterNodeStatus(const std::string& name, uint32 fee, uint32 rtime) :
        mNodeName(name),
        mLoadFee(fee),
        mReportTime(rtime)
    { ; }

    std::string const& getName()
    {
        return mNodeName;
    }

    uint32 getLoadFee()
    {
        return mLoadFee;
    }

    uint32 getReportTime()
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
    uint32            mLoadFee;
    uint32            mReportTime;
};

#endif
