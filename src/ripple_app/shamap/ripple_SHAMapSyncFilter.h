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
    virtual ~SHAMapSyncFilter () { }

    // Note that the nodeData is overwritten by this call
    virtual void gotNode (bool fromFilter,
                          SHAMapNode const& id,
                          uint256 const& nodeHash,
                          Blob& nodeData,
                          SHAMapTreeNode::TNType type) = 0;

    virtual bool haveNode (SHAMapNode const& id,
                           uint256 const& nodeHash,
                           Blob& nodeData) = 0;
};

#endif
