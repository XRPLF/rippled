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

/** Calculates payment paths.

    The \ref RippleCalc determines the quality of the found paths.

    \see RippleCalc
*/
class Pathfinder
{
public:
    Pathfinder (
        RippleLineCache::ref cache,
        RippleAddress const& srcAccountID,
        RippleAddress const& dstAccountID,
        Currency const& srcCurrencyID,
        Account const& srcIssuerID,
        STAmount const& dstAmount,
        bool& bValid);

    static void initPathTable();

    bool findPaths (
        int iLevel,
        unsigned int const iMaxPaths,
        STPathSet& spsDst,
        STPath& spExtraPath);

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
        nt_SOURCE,     // The source account with an issuer account, if required
        nt_ACCOUNTS,   // Accounts that connect from this source/currency
        nt_BOOKS,      // Order books that connect to this currency
        nt_XRP_BOOK,   // The order book from this currency to XRP
        nt_DEST_BOOK,  // The order book to the destination currency/issuer
        nt_DESTINATION // The destination account only
    };

    typedef std::vector<NodeType>         PathType_t;
    typedef std::pair<int, PathType_t>    CostedPath_t;
    typedef std::vector<CostedPath_t>     CostedPathList_t;

    typedef std::pair<int, const char*>   PathCost;
    typedef std::vector<PathCost>         PathCostList;

    typedef std::map<PaymentType, CostedPathList_t> PathTable;


    /** Fill a CostedPathList_t from its description. */
    static void fillPaths(PaymentType type,
                          PathCostList const& costs);

    /** \return true if any building paths are now complete. */
    bool checkComplete (STPathSet& retPathSet);

    static std::string pathTypeToString(PathType_t const&);

    bool matchesOrigin (Issue const&);

    int getPathsOut (
        Currency const& currency,
        Account const& accountID,
        bool isDestCurrency,
        Account const& dest);

    void addLink(
        STPath const& currentPath,
        STPathSet& incompletePaths,
        int addFlags);

    void addLink(
        STPathSet const& currentPaths,
        STPathSet& incompletePaths,
        int addFlags);

    STPathSet& getPaths(PathType_t const& type,
                        bool addComplete = true);

    STPathSet filterPaths(int iMaxPaths,
                          STPath& extraPath);

    bool isNoRippleOut (STPath const& currentPath);
    bool isNoRipple (
        Account const& setByID,
        Account const& setOnID,
        Currency const& currencyID);

    // Our main table of paths

    static PathTable mPathTable;
    static PathType_t makePath(char const*);

    Account             mSrcAccountID;
    Account             mDstAccountID;
    STAmount            mDstAmount;
    Currency            mSrcCurrencyID;
    Account             mSrcIssuerID;
    STAmount            mSrcAmount;

    Ledger::pointer     mLedger;
    LoadEvent::pointer  m_loadEvent;
    RippleLineCache::pointer    mRLCache;

    STPathElement mSource;
    STPathSet mCompletePaths;
    std::map<PathType_t, STPathSet> mPaths;

    hash_map<Issue, int> mPathsOutCountMap;

    // Add ripple paths
    static std::uint32_t const afADD_ACCOUNTS = 0x001;

    // Add order books
    static std::uint32_t const afADD_BOOKS    = 0x002;

    // Add order book to XRP only
    static std::uint32_t const afOB_XRP       = 0x010;

    // Must link to destination currency
    static std::uint32_t const afOB_LAST      = 0x040;

    // Destination account only
    static std::uint32_t const afAC_LAST      = 0x080;
};

CurrencySet usAccountDestCurrencies
        (RippleAddress const& raAccountID,
         RippleLineCache::ref cache,
         bool includeXRP);

CurrencySet usAccountSourceCurrencies
        (RippleAddress const& raAccountID,
         RippleLineCache::ref lrLedger,
         bool includeXRP);

} // ripple

#endif
