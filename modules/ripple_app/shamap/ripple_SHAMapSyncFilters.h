//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SHAMAPSYNCFILTERS_H
#define RIPPLE_SHAMAPSYNCFILTERS_H

// Sync filters allow low-level SHAMapSync code to interact correctly with
// higher-level structures such as caches and transaction stores

// This class is needed on both add and check functions
// sync filter for transaction sets during consensus building
class ConsensusTransSetSF : public SHAMapSyncFilter
{
public:
    ConsensusTransSetSF ();

    // Note that the nodeData is overwritten by this call
    void gotNode (bool fromFilter,
                  SHAMapNode const& id,
                  uint256 const& nodeHash,
                  Blob& nodeData,
                  SHAMapTreeNode::TNType);

    bool haveNode (SHAMapNode const& id,
                   uint256 const& nodeHash,
                   Blob& nodeData);
};

// This class is only needed on add functions
// sync filter for account state nodes during ledger sync
class AccountStateSF : public SHAMapSyncFilter
{
public:
    explicit AccountStateSF (uint32 ledgerSeq);

    // Note that the nodeData is overwritten by this call
    void gotNode (bool fromFilter,
                  SHAMapNode const& id,
                  uint256 const& nodeHash,
                  Blob& nodeData,
                  SHAMapTreeNode::TNType);

    bool haveNode (SHAMapNode const& id,
                   uint256 const& nodeHash,
                   Blob& nodeData);

private:
    uint32 mLedgerSeq;
};

// This class is only needed on add functions
// sync filter for transactions tree during ledger sync
class TransactionStateSF : public SHAMapSyncFilter
{
public:
    explicit TransactionStateSF (uint32 ledgerSeq);

    // Note that the nodeData is overwritten by this call
    void gotNode (bool fromFilter,
                  SHAMapNode const& id,
                  uint256 const& nodeHash,
                  Blob& nodeData,
                  SHAMapTreeNode::TNType);

    bool haveNode (SHAMapNode const& id,
                   uint256 const& nodeHash,
                   Blob& nodeData);

private:
    uint32 mLedgerSeq;
};

#endif
