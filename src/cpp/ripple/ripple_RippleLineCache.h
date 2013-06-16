//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RIPPLELINECACHE_H
#define RIPPLE_RIPPLELINECACHE_H

// Used by Pathfinder
class RippleLineCache
{
public:
    typedef boost::shared_ptr <RippleLineCache> pointer;
    typedef pointer const& ref;

    explicit RippleLineCache (Ledger::ref l)
        : mLedger (l)
    {
    }

    Ledger::ref getLedger () // VFALCO TODO const?
    {
        return mLedger;
    }

    AccountItems& getRippleLines (const uint160& accountID);

private:
    boost::mutex mLock;
    
    Ledger::pointer mLedger;
    
    boost::unordered_map <uint160, AccountItems::pointer> mRLMap;
};

#endif
