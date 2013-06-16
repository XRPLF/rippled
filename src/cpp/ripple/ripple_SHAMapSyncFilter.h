//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SHAMAPSYNCFILTER_H
#define RIPPLE_SHAMAPSYNCFILTER_H

/** Callback for filtering SHAMap during sync.
*/
class SHAMapSyncFilter
{
public:
    SHAMapSyncFilter ()
    {
    }

    virtual ~SHAMapSyncFilter ()
    {
    }

    virtual void gotNode (bool fromFilter,
                          SHAMapNode const& id,
                          uint256 const& nodeHash,
                          Blob const& nodeData,
                          SHAMapTreeNode::TNType type)
    {
    }

    virtual bool haveNode (SHAMapNode const& id,
                           uint256 const& nodeHash,
                           Blob& nodeData)
    {
        return false;
    }
};

#endif
// vim:ts=4
