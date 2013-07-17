//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_PATHFINDER_H
#define RIPPLE_PATHFINDER_H

// VFALCO TODO Remove this unused stuff?
#if 0
//
// This is a very simple implementation. This can be made way better.
// We are simply flooding from the start. And doing an exhaustive search of all paths under maxSearchSteps. An easy improvement would
be to flood from both directions.
//

class PathOption
{
public:
    typedef boost::shared_ptr<PathOption> pointer;
    typedef const boost::shared_ptr<PathOption>& ref;

    STPath      mPath;
    bool        mCorrectCurrency;   // for the sorting
    uint160     mCurrencyID;        // what currency we currently have at the end of the path
    uint160     mCurrentAccount;    // what account is at the end of the path
    int         mTotalCost;         // in send currency
    STAmount    mMinWidth;          // in dest currency
    float       mQuality;

    PathOption (uint160& srcAccount, uint160& srcCurrencyID, const uint160& dstCurrencyID);
    PathOption (PathOption::pointer other);
};
#endif

/** Calculates payment paths.

    The @ref RippleCalc determines the quality of the found paths.

    @see RippleCalc
*/
class Pathfinder
{
public:
    Pathfinder (RippleLineCache::ref cache,
                const RippleAddress& srcAccountID, const RippleAddress& dstAccountID,
                const uint160& srcCurrencyID, const uint160& srcIssuerID, const STAmount& dstAmount, bool& bValid);

    bool findPaths (const unsigned int iMaxSteps, const unsigned int iMaxPaths, STPathSet& spsDst);

    bool bDefaultPath (const STPath& spPath);

private:
    //  void addOptions(PathOption::pointer tail);

    // returns true if any building paths are now complete?
    bool checkComplete (STPathSet& retPathSet);

    //  void addPathOption(PathOption::pointer pathOption);

    bool matchesOrigin (const uint160& currency, const uint160& issuer);

    int getPathsOut (const uint160& currency, const uint160& accountID,
                     bool isDestCurrency, const uint160& dest);

private:
    uint160             mSrcAccountID;
    uint160             mDstAccountID;
    STAmount            mDstAmount;
    uint160             mSrcCurrencyID;
    uint160             mSrcIssuerID;
    STAmount            mSrcAmount;

    Ledger::pointer     mLedger;
    PathState::pointer  mPsDefault;
    LoadEvent::pointer  m_loadEvent;
    RippleLineCache::pointer    mRLCache;

    boost::unordered_map<uint160, AccountItems::pointer>    mRLMap;
    boost::unordered_map<std::pair<uint160, uint160>, int>  mPOMap;

    //  std::list<PathOption::pointer> mBuildingPaths;
    //  std::list<PathOption::pointer> mCompletePaths;
};

boost::unordered_set<uint160> usAccountDestCurrencies (const RippleAddress& raAccountID, Ledger::ref lrLedger,
        bool includeXRP);

boost::unordered_set<uint160> usAccountSourceCurrencies (const RippleAddress& raAccountID, Ledger::ref lrLedger,
        bool includeXRP);

#endif
