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

#ifndef RIPPLE_PATHFINDER_H
#define RIPPLE_PATHFINDER_H

namespace ripple {

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
    typedef std::shared_ptr<PathOption> pointer;
    typedef const std::shared_ptr<PathOption>& ref;

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

    static void initPathTable();
    bool findPaths (int iLevel, const unsigned int iMaxPaths, STPathSet& spsDst, STPath& spExtraPath);

private:

    enum PaymentType
    {
        pt_XRP_to_XRP,
        pt_XRP_to_nonXRP,
        pt_nonXRP_to_XRP,
        pt_nonXRP_to_same,
        pt_nonXRP_to_nonXRP
    };

    enum NodeType
    {
        nt_SOURCE,       // The source account with an issuer account, if required
        nt_ACCOUNTS,     // Accounts that connect from this source/currency
        nt_BOOKS,        // Order books that connect to this currency
        nt_XRP_BOOK,     // The order book from this currency to XRP
        nt_DEST_BOOK,    // The order book to the destination currency/issuer
        nt_DESTINATION   // The destination account only
    };

    typedef std::vector<NodeType>         PathType_t;
    typedef std::pair<int, PathType_t>    CostedPath_t;
    typedef std::vector<CostedPath_t>     CostedPathList_t;

    // returns true if any building paths are now complete?
    bool checkComplete (STPathSet& retPathSet);

    static std::string pathTypeToString(PathType_t const&);

    bool matchesOrigin (const uint160& currency, const uint160& issuer);

    int getPathsOut (const uint160& currency, const uint160& accountID,
                     bool isDestCurrency, const uint160& dest);

    void addLink(const STPath& currentPath, STPathSet& incompletePaths, int addFlags);
    void addLink(const STPathSet& currentPaths, STPathSet& incompletePaths, int addFlags);
    STPathSet& getPaths(const PathType_t& type, bool addComplete = true);
    STPathSet filterPaths(int iMaxPaths, STPath& extraPath);

    bool isNoRippleOut (const STPath& currentPath);
    bool isNoRipple (uint160 const& setByID, uint160 const& setOnID, uint160 const& currencyID);

    // Our main table of paths

    static std::map<PaymentType, CostedPathList_t> mPathTable;
    static PathType_t makePath(char const*);

    uint160             mSrcAccountID;
    uint160             mDstAccountID;
    STAmount            mDstAmount;
    uint160             mSrcCurrencyID;
    uint160             mSrcIssuerID;
    STAmount            mSrcAmount;

    Ledger::pointer     mLedger;
    LoadEvent::pointer  m_loadEvent;
    RippleLineCache::pointer    mRLCache;

    STPathElement                     mSource;
    STPathSet                         mCompletePaths;
    std::map< PathType_t, STPathSet > mPaths;

    ripple::unordered_map<uint160, AccountItems::pointer>    mRLMap;
    ripple::unordered_map<std::pair<uint160, uint160>, int>  mPOMap;

    static const std::uint32_t afADD_ACCOUNTS = 0x001;  // Add ripple paths
    static const std::uint32_t afADD_BOOKS    = 0x002;  // Add order books
    static const std::uint32_t afOB_XRP       = 0x010;  // Add order book to XRP only
    static const std::uint32_t afOB_LAST      = 0x040;  // Must link to destination currency
    static const std::uint32_t afAC_LAST      = 0x080;  // Destination account only
};

boost::unordered_set<uint160> usAccountDestCurrencies
        (const RippleAddress& raAccountID,
        RippleLineCache::ref cache,
        bool includeXRP);

boost::unordered_set<uint160> usAccountSourceCurrencies
        (const RippleAddress& raAccountID,
        RippleLineCache::ref lrLedger,
        bool includeXRP);

} // ripple

#endif
