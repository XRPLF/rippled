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

#include <ripple/app/paths/Tuning.h>

#include <tuple>

namespace ripple {

/*
we just need to find a succession of the highest quality paths there until we
find enough width

Don't do branching within each path

We have a list of paths we are working on but how do we compare the ones that
are terminating in a different currency?

Loops

TODO: what is a good way to come up with multiple paths?
    Maybe just change the sort criteria?
    first a low cost one and then a fat short one?


OrderDB:
    getXRPOffers();

    // return list of all orderbooks that want XRP
    // return list of all orderbooks that want IssuerID
    // return list of all orderbooks that want this issuerID and currencyID
*/

/*
Test sending to XRP
Test XRP to XRP
Test offer in middle
Test XRP to USD
Test USD to EUR
*/

// we sort the options by:
//    cost of path
//    length of path
//    width of path
//    correct currency at the end

// quality, length, liquidity, index
typedef std::tuple<std::uint64_t, int, STAmount, unsigned int> path_LQ_t;

// Lower numbers have better quality. Sort higher quality first.
static bool bQualityCmp (path_LQ_t const& a, path_LQ_t const& b)
{
    // 1) Higher quality (lower cost) is better
    if (std::get<0> (a) != std::get<0> (b))
        return std::get<0> (a) < std::get<0> (b);

    // 2) More liquidity (higher volume) is better
    if (std::get<2> (a) != std::get<2> (b))
        return std::get<2> (a) > std::get<2> (b);

    // 3) Shorter paths are better
    if (std::get<1> (a) != std::get<1> (b))
        return std::get<1> (a) < std::get<1> (b);

    // 4) Tie breaker
    return std::get<3> (a) > std::get<3> (b);
}

typedef std::pair<int, Account> AccountCandidate;
typedef std::vector<AccountCandidate> AccountCandidates;

static bool candCmp (
    std::uint32_t seq,
    AccountCandidate const& first, AccountCandidate const& second)
{
    if (first.first < second.first)
        return false;

    if (first.first > second.first)
        return true;

    return (first.first ^ seq) < (second.first ^ seq);
}

Pathfinder::Pathfinder (
    RippleLineCache::ref cache,
    RippleAddress const& uSrcAccountID,
    RippleAddress const& uDstAccountID,
    Currency const& uSrcCurrencyID,
    Account const& uSrcIssuerID,
    STAmount const& saDstAmount,
    bool& bValid)
    :   mSrcAccountID (uSrcAccountID.getAccountID ()),
        mDstAccountID (uDstAccountID.getAccountID ()),
        mDstAmount (saDstAmount),
        mSrcCurrencyID (uSrcCurrencyID),
        mSrcIssuerID (uSrcIssuerID),
        mSrcAmount ({uSrcCurrencyID, uSrcIssuerID}, 1u, 0, true),
        mLedger (cache->getLedger ()), mRLCache (cache)
{

    if ((mSrcAccountID == mDstAccountID &&
         mSrcCurrencyID == mDstAmount.getCurrency ()) || mDstAmount == zero)
    {
        // No need to send to same account with same currency, must send
        // non-zero.
        bValid = false;
        mLedger.reset ();
        return;
    }

    bValid = true;

    m_loadEvent = getApp().getJobQueue ().getLoadEvent (
        jtPATH_FIND, "FindPath");

    bool bIssuer = mSrcCurrencyID.isNonZero() &&
            mSrcIssuerID.isNonZero() &&
            (mSrcIssuerID != mSrcAccountID);
    mSource = STPathElement(
        // Where does an empty path start?
        bIssuer ? mSrcIssuerID : mSrcAccountID,
        // On the source account or issuer account
        mSrcCurrencyID,
        // In the source currency
        mSrcCurrencyID.isZero() ? Account() :
        (bIssuer ? mSrcIssuerID : mSrcAccountID));
}

bool Pathfinder::findPaths (
    int iLevel, const unsigned int iMaxPaths,
    STPathSet& pathsOut, STPath& extraPath)
{
    // pathsOut contains only non-default paths without source or
    // destination. On input, pathsOut contains any paths you want to ensure are
    // included if still good.

    WriteLog (lsTRACE, Pathfinder)
            << "findPaths>"
            << " mSrcAccountID=" << mSrcAccountID
            << " mDstAccountID=" << mDstAccountID
            << " mDstAmount=" << mDstAmount.getFullText ()
            << " mSrcCurrencyID=" << mSrcCurrencyID
            << " mSrcIssuerID=" << mSrcIssuerID;

    if (!mLedger)
    {
        WriteLog (lsDEBUG, Pathfinder) << "findPaths< no ledger";

        return false;
    }

    bool bSrcXrp       = mSrcCurrencyID.isZero();
    bool bDstXrp       = mDstAmount.getCurrency().isZero();

    auto sleSrc = mLedger->getSLEi(Ledger::getAccountRootIndex(mSrcAccountID));
    if (!sleSrc)
        return false;

    auto sleDest = mLedger->getSLEi(Ledger::getAccountRootIndex(mDstAccountID));
    if (!sleDest && (!bDstXrp || (mDstAmount < mLedger->getReserve(0))))
        return false;

    PaymentType paymentType;
    if (bSrcXrp && bDstXrp)
    { // XRP -> XRP

        WriteLog (lsDEBUG, Pathfinder) << "XRP to XRP payment";
        paymentType = pt_XRP_to_XRP;

    }
    else if (bSrcXrp)
    { // XRP -> non-XRP

        WriteLog (lsDEBUG, Pathfinder) << "XRP to non-XRP payment";
        paymentType = pt_XRP_to_nonXRP;

    }
    else if (bDstXrp)
    { // non-XRP -> XRP

        WriteLog (lsDEBUG, Pathfinder) << "non-XRP to XRP payment";
        paymentType = pt_nonXRP_to_XRP;

    }
    else if (mSrcCurrencyID == mDstAmount.getCurrency())
    { // non-XRP -> non-XRP - Same currency

        WriteLog (lsDEBUG, Pathfinder) << "non-XRP to non-XRP - same currency";
        paymentType = pt_nonXRP_to_same;

    }
    else
    { // non-XRP to non-XRP - Different currency

        WriteLog (lsDEBUG, Pathfinder) << "non-XRP to non-XRP - cross currency";
        paymentType = pt_nonXRP_to_nonXRP;

    }

    for (auto const& costedPath : mPathTable[paymentType])
    {
       if (costedPath.first <= iLevel)
       {
           getPaths(costedPath.second);
           if (mCompletePaths.size () > PATHFINDER_MAX_COMPLETE_PATHS)
               break;
       }
    }

    WriteLog (lsDEBUG, Pathfinder)
            << mCompletePaths.size() << " complete paths found";

    for (auto const& path : pathsOut)
    { // make sure no paths were lost
        bool found = false;
        if (!path.empty ())
        {
            for (auto const& ePath : mCompletePaths)
            {
                if (ePath == path)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                mCompletePaths.push_back (path);
        }
    }

    WriteLog (lsDEBUG, Pathfinder)
        << mCompletePaths.size() << " paths to filter";

    if (mCompletePaths.size() > iMaxPaths)
        pathsOut = filterPaths(iMaxPaths, extraPath);
    else
        pathsOut = mCompletePaths;

    // Even if we find no paths, default paths may work, and we don't check them
    // currently.
    return true;
}

// Check the specified path
// Returning the initial quality and liquidity
TER Pathfinder::checkPath (
    STPath const& path,            // The path to check
    STAmount const& minDstAmount,  // The minimum output this path must
                                   // deliver to be worth keeping
    STAmount& amountOut,           // The returned liquidity
    uint64_t& qualityOut) const    // The returned initial quality
{
    STPathSet pathSet;
    pathSet.push_back (path);

    // We only want to look at this path
    path::RippleCalc::Input rcInput;
    rcInput.defaultPathsAllowed = false;

    LedgerEntrySet scratchPad (mLedger, tapNONE);

    try
    {
        // Try to move minimum amount to sanity-check
        // path and compute initial quality
        auto rc = path::RippleCalc::rippleCalculate (
            scratchPad, mSrcAmount, minDstAmount,
            mDstAccountID, mSrcAccountID,
            pathSet, &rcInput);

        if (rc.result() != tesSUCCESS)
        {
            // Path has trivial/no liquidity
            return rc.result();
        }

        qualityOut = getRate
            (rc.actualAmountOut, rc.actualAmountIn);
        amountOut = rc.actualAmountOut;

        // Try to complete as much of the payment
        // as possible to assess path liquidity
        rcInput.partialPaymentAllowed = true;
        rc = path::RippleCalc::rippleCalculate (
            scratchPad, mSrcAmount, mDstAmount - amountOut,
            mDstAccountID, mSrcAccountID, pathSet, &rcInput);
        if (rc.result() == tesSUCCESS)
        {
            // Report total liquidity to caller
            amountOut += rc.actualAmountOut;
        }

        return tesSUCCESS;
    }
    catch (std::exception const& e)
    {
        WriteLog (lsINFO, Pathfinder) <<
            "checkpath: exception (" << e.what() << ") " <<
            path.getJson (0);
        return tefEXCEPTION;
    }
}

STPathSet Pathfinder::filterPaths(int iMaxPaths, STPath& extraPath)
{
    if (mCompletePaths.size() <= iMaxPaths)
        return mCompletePaths;

    STAmount remaining = mDstAmount;

    // Must subtract liquidity in default path from remaining amount
    try
    {
        LedgerEntrySet lesSandbox (mLedger, tapNONE);

        path::RippleCalc::Input rcInput;
        rcInput.partialPaymentAllowed = true;
        auto rc = path::RippleCalc::rippleCalculate (
            lesSandbox,
            mSrcAmount,
            mDstAmount,
            mDstAccountID,
            mSrcAccountID,
            STPathSet(),
            &rcInput);

        if (rc.result () == tesSUCCESS)
        {
            WriteLog (lsDEBUG, Pathfinder)
                    << "Default path contributes: " << rc.actualAmountIn;
            remaining -= rc.actualAmountOut;
        }
        else
        {
            WriteLog (lsDEBUG, Pathfinder)
                << "Default path fails: " << transToken (rc.result ());
        }
    }
    catch (...)
    {
        WriteLog (lsDEBUG, Pathfinder) << "Default path causes exception";
    }

    std::vector<path_LQ_t> vMap;

    // Ignore paths that move only very small amounts
    auto saMinDstAmount = divide(
        mDstAmount, STAmount(iMaxPaths + 2), mDstAmount);

    // Build map of quality to entry.
    for (int i = 0; i < mCompletePaths.size(); ++i)
    {
        auto const& currentPath = mCompletePaths[i];

        STAmount actualOut;
        uint64_t uQuality;
        auto const resultCode = checkPath
            (currentPath, saMinDstAmount, actualOut, uQuality);

        if (resultCode != tesSUCCESS)
        {
            WriteLog (lsDEBUG, Pathfinder) <<
                "findPaths: dropping: " << transToken (resultCode) <<
                ": " << currentPath.getJson (0);
        }
        else
        {
            WriteLog (lsDEBUG, Pathfinder) <<
                "findPaths: quality: " << uQuality <<
                ": " << currentPath.getJson (0);

            vMap.push_back (path_LQ_t (
                uQuality, currentPath.size (), actualOut, i));
        }
    }

    STPathSet spsDst;

    if (vMap.size())
    {
        // Lower is better and should be first.
        std::sort (vMap.begin (), vMap.end (), bQualityCmp);

        for (int i = 0, iPathsLeft = iMaxPaths;
             (iPathsLeft > 0 || extraPath.empty()) && i < vMap.size (); ++i)
        {
            path_LQ_t& lqt = vMap[i];

            if (iPathsLeft > 1 ||
                (iPathsLeft > 0 && std::get<2> (lqt) >= remaining))
            {
                // last path must fill
                --iPathsLeft;
                remaining -= std::get<2> (lqt);
                spsDst.push_back (mCompletePaths[std::get<3> (lqt)]);
            }
            else if (iPathsLeft == 0 && std::get<2>(lqt) >= mDstAmount &&
                     extraPath.empty())
            {
                // found an extra path that can move the whole amount
                extraPath = mCompletePaths[std::get<3>(lqt)];
                WriteLog (lsDEBUG, Pathfinder) <<
                    "Found extra full path: " << extraPath.getJson(0);
            }
            else
                WriteLog (lsDEBUG, Pathfinder) <<
                    "Skipping a non-filling path: " <<
                    mCompletePaths[std::get<3> (lqt)].getJson (0);
        }

        if (remaining > zero)
        {
            WriteLog (lsINFO, Pathfinder) <<
                "Paths could not send " << remaining << " of " << mDstAmount;
        }
        else
        {
            WriteLog (lsDEBUG, Pathfinder) <<
                "findPaths: RESULTS: " << spsDst.getJson (0);
        }
    }
    else
    {
        WriteLog (lsDEBUG, Pathfinder) <<
            "findPaths: RESULTS: non-defaults filtered away";
    }

    return spsDst;
}

CurrencySet usAccountSourceCurrencies (
    RippleAddress const& raAccountID, RippleLineCache::ref lrCache,
    bool includeXRP)
{
    CurrencySet usCurrencies;

    // YYY Only bother if they are above reserve
    if (includeXRP)
        usCurrencies.insert (xrpCurrency());

    // List of ripple lines.
    auto& rippleLines (lrCache->getRippleLines (raAccountID.getAccountID ()));

    for (auto const& item : rippleLines)
    {
        auto rspEntry = (RippleState*) item.get ();
        auto& saBalance = rspEntry->getBalance ();

        // Filter out non
        if (saBalance > zero
            // Have IOUs to send.
            || (rspEntry->getLimitPeer ()
                // Peer extends credit.
                && ((-saBalance) < rspEntry->getLimitPeer ()))) // Credit left.
        {
            usCurrencies.insert (saBalance.getCurrency ());
        }
    }

    usCurrencies.erase (badCurrency());
    return usCurrencies;
}

CurrencySet usAccountDestCurrencies (
    RippleAddress const& raAccountID,
    RippleLineCache::ref lrCache,
    bool includeXRP)
{
    CurrencySet usCurrencies;

    if (includeXRP)
        usCurrencies.insert (xrpCurrency());
    // Even if account doesn't exist

    // List of ripple lines.
    auto& rippleLines (lrCache->getRippleLines (raAccountID.getAccountID ()));

    for (auto const& item : rippleLines)
    {
        RippleState*    rspEntry    = (RippleState*) item.get ();
        STAmount const& saBalance   = rspEntry->getBalance ();

        if (saBalance < rspEntry->getLimit ())                  // Can take more
            usCurrencies.insert (saBalance.getCurrency ());
    }

    usCurrencies.erase (badCurrency());
    return usCurrencies;
}

bool Pathfinder::matchesOrigin (Issue const& issue)
{
    return issue.currency == mSrcCurrencyID &&
            (isXRP (issue) ||
             issue.account == mSrcIssuerID ||
             issue.account == mSrcAccountID);
}

int Pathfinder::getPathsOut (
    Currency const& currencyID, Account const& accountID,
    bool isDstCurrency, Account const& dstAccount)
{
    Issue const issue (currencyID, accountID);

    auto it = mPathsOutCountMap.emplace (issue, 0);

    // If it was already present, return the stored number of paths
    if (!it.second)
        return it.first->second;

    auto sleAccount = mLedger->getSLEi (Ledger::getAccountRootIndex (accountID));

    if (!sleAccount)
        return 0;

    int aFlags = sleAccount->getFieldU32(sfFlags);
    bool const bAuthRequired = (aFlags & lsfRequireAuth) != 0;
    bool const bFrozen = ((aFlags & lsfGlobalFreeze) != 0)
        && mLedger->enforceFreeze ();

    int count = 0;

    if (!bFrozen)
    {
        count = getApp().getOrderBookDB().getBookSize(issue);

        for (auto const& item : mRLCache->getRippleLines (accountID))
        {
            RippleState* rspEntry = (RippleState*) item.get ();

            if (currencyID != rspEntry->getLimit ().getCurrency ())
            {
            }
            else if (rspEntry->getBalance () <= zero &&
                     (!rspEntry->getLimitPeer ()
                      || -rspEntry->getBalance () >= rspEntry->getLimitPeer ()
                      ||  (bAuthRequired && !rspEntry->getAuth ())))
            {
            }
            else if (isDstCurrency && (dstAccount == rspEntry->getAccountIDPeer ()))
                count += 10000; // count a path to the destination extra
            else if (rspEntry->getNoRipplePeer ())
            {
                // This probably isn't a useful path out
            }
            else if (rspEntry->getFreezePeer () && mLedger->enforceFreeze ())
            {
                // Not a useful path out
            }
            else
                ++count;
        }
    }
    it.first->second = count;
    return count;
}

void Pathfinder::addLink(
    STPathSet const& currentPaths,  // The paths to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags)
{
    WriteLog (lsDEBUG, Pathfinder)
        << "addLink< on " << currentPaths.size()
        << " source(s), flags=" << addFlags;
    for (auto const& path: currentPaths)
        addLink(path, incompletePaths, addFlags);
}

STPathSet& Pathfinder::getPaths(PathType_t const& type, bool addComplete)
{
    auto it = mPaths.find(type);

    // We already have these paths
    if (it != mPaths.end())
        return it->second;

    // The type is empty
    if (type.empty())
        return mPaths[type];

    NodeType toAdd = type.back();
    PathType_t pathType(type);
    pathType.pop_back();

    STPathSet pathsIn = getPaths(pathType, false);
    STPathSet& pathsOut = mPaths[type];

    WriteLog (lsDEBUG, Pathfinder)
        << "getPaths< adding onto '"
        << pathTypeToString(pathType) << "' to get '"
        << pathTypeToString(type) << "'";

    int cp = mCompletePaths.size();

    switch (toAdd)
    {
    case nt_SOURCE:
        // source is an empty path
        assert(pathsOut.empty());
        pathsOut.push_back (STPath());
        break;

    case nt_ACCOUNTS:
        addLink(pathsIn, pathsOut, afADD_ACCOUNTS);
        break;

    case nt_BOOKS:
        addLink(pathsIn, pathsOut, afADD_BOOKS);
        break;

    case nt_XRP_BOOK:
        addLink(pathsIn, pathsOut, afADD_BOOKS | afOB_XRP);
        break;

    case nt_DEST_BOOK:
        addLink(pathsIn, pathsOut, afADD_BOOKS | afOB_LAST);
        break;

    case nt_DESTINATION:
        // FIXME: What if a different issuer was specified on the
        // destination amount?
        addLink(pathsIn, pathsOut, afADD_ACCOUNTS | afAC_LAST);
        break;
    }

    CondLog (mCompletePaths.size() != cp, lsDEBUG, Pathfinder)
        << (mCompletePaths.size() - cp)
        << " complete paths added";
    WriteLog (lsDEBUG, Pathfinder)
        << "getPaths> " << pathsOut.size() << " partial paths found";
    return pathsOut;
}

bool Pathfinder::isNoRipple (
    Account const& setByID, Account const& setOnID, Currency const& currencyID)
{
    SLE::pointer sleRipple = mLedger->getSLEi (
        Ledger::getRippleStateIndex (setByID, setOnID, currencyID));

    auto const flag ((setByID > setOnID) ? lsfHighNoRipple : lsfLowNoRipple);

    return sleRipple && (sleRipple->getFieldU32 (sfFlags) & flag);
}

// Does this path end on an account-to-account link whose last account
// has set no ripple on the link?
bool Pathfinder::isNoRippleOut (STPath const& currentPath)
{
    // Must have at least one link
    if (currentPath.size() == 0)
        return false;

    // Last link must be an account
    STPathElement const& endElement = *(currentPath.end() - 1);
    if (!(endElement.getNodeType() & STPathElement::typeAccount))
        return false;

    // What account are we leaving?
    auto const& fromAccount = (currentPath.size() == 1)
        ? mSrcAccountID
        : (currentPath.end() - 2)->getAccountID ();

    return isNoRipple (
        endElement.getAccountID (), fromAccount, endElement.getCurrency ());
}

void Pathfinder::addLink(
    const STPath& currentPath,      // The path to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags)
{
    auto const& pathEnd = currentPath.empty()
        ? mSource
        : currentPath.back ();
    auto const& uEndCurrency    = pathEnd.getCurrency ();
    auto const& uEndIssuer      = pathEnd.getIssuerID ();
    auto const& uEndAccount     = pathEnd.getAccountID ();
    bool const bOnXRP = uEndCurrency.isZero();

    WriteLog (lsTRACE, Pathfinder) << "addLink< flags="
                                   << addFlags << " onXRP=" << bOnXRP;
    WriteLog (lsTRACE, Pathfinder) << currentPath.getJson(0);

    auto add_unique_path = [](STPathSet& path_set, STPath const& path)
    {
        for (auto const& p : path_set)
        {
            if (p == path)
                return;
        }
        path_set.push_back (path);
    };

    if (addFlags & afADD_ACCOUNTS)
    { // add accounts
        if (bOnXRP)
        {
            if (mDstAmount.isNative() && !currentPath.empty())
            { // non-default path to XRP destination
                WriteLog (lsTRACE, Pathfinder)
                    << "complete path found ax: " << currentPath.getJson(0);
                add_unique_path (mCompletePaths, currentPath);
            }
        }
        else
        { // search for accounts to add
            auto sleEnd = mLedger->getSLEi(
                Ledger::getAccountRootIndex(uEndAccount));
            if (sleEnd)
            {
                bool const bRequireAuth (
                    sleEnd->getFieldU32(sfFlags) & lsfRequireAuth);
                bool const bIsEndCurrency (
                    uEndCurrency == mDstAmount.getCurrency());
                bool const bIsNoRippleOut (
                    isNoRippleOut (currentPath));
                bool const bDestOnly (
                    addFlags & afAC_LAST);

                auto& rippleLines (mRLCache->getRippleLines(uEndAccount));

                AccountCandidates candidates;
                candidates.reserve(rippleLines.size());

                for(auto const& item : rippleLines)
                {
                    auto* rs = dynamic_cast<RippleState const *> (item.get());
                    if (!rs)
                    {
                        WriteLog (lsERROR, Pathfinder)
                                << "Couldn't decipher RippleState";
                        continue;
                    }
                    auto const& acctID = rs->getAccountIDPeer();
                    bool const bToDestination = acctID == mDstAccountID;

                    if (bDestOnly && !bToDestination)
                    {
                        continue;
                    }

                    if ((uEndCurrency == rs->getLimit().getCurrency()) &&
                        !currentPath.hasSeen(acctID, uEndCurrency, acctID))
                    {
                        // path is for correct currency and has not been seen
                        if (rs->getBalance() <= zero
                            && (!rs->getLimitPeer()
                                || -rs->getBalance() >= rs->getLimitPeer()
                                || (bRequireAuth && !rs->getAuth())))
                        {
                            // path has no credit
                        }
                        else if (bIsNoRippleOut && rs->getNoRipple())
                        {
                            // Can't leave on this path
                        }
                        else if (bToDestination)
                        {
                            // destination is always worth trying
                            if (uEndCurrency == mDstAmount.getCurrency())
                            {
                                // this is a complete path
                                if (!currentPath.empty())
                                {
                                    WriteLog (lsTRACE, Pathfinder)
                                            << "complete path found ae: "
                                            << currentPath.getJson(0);
                                    add_unique_path (mCompletePaths, currentPath);
                                }
                            }
                            else if (!bDestOnly)
                            {
                                // this is a high-priority candidate
                                candidates.push_back(std::make_pair(100000, acctID));
                            }
                        }
                        else if (acctID == mSrcAccountID)
                        {
                            // going back to the source is bad
                        }
                        else
                        {
                            // save this candidate
                            int out = getPathsOut(uEndCurrency, acctID, bIsEndCurrency, mDstAccountID);
                            if (out)
                                candidates.push_back(std::make_pair(out, acctID));
                        }
                    }
                }

                if (!candidates.empty())
                {
                    std::sort (candidates.begin(), candidates.end(),
                        std::bind(candCmp, mLedger->getLedgerSeq(),
                                  std::placeholders::_1,
                                  std::placeholders::_2));

                    int count = candidates.size();
                    // allow more paths from source
                    if ((count > 10) && (uEndAccount != mSrcAccountID))
                        count = 10;
                    else if (count > 50)
                        count = 50;

                    auto it = candidates.begin();
                    while (count-- != 0)
                    {
                        // Add accounts to incompletePaths
                        incompletePaths.assembleAdd(
                            currentPath,
                            STPathElement(STPathElement::typeAccount,
                                          it->second, uEndCurrency,
                                          it->second));
                        ++it;
                    }
                }

            }
            else
            {
                WriteLog(lsWARNING, Pathfinder)
                    << "Path ends on non-existent issuer";
            }
        }
    }
    if (addFlags & afADD_BOOKS)
    { // add order books
        if (addFlags & afOB_XRP)
        { // to XRP only
            if (!bOnXRP && getApp().getOrderBookDB().isBookToXRP (
                    {uEndCurrency, uEndIssuer}))
            {
                STPathElement pathElement(
                    STPathElement::typeCurrency,
                    xrpAccount(), xrpCurrency(), xrpAccount());
                incompletePaths.assembleAdd(currentPath, pathElement);
            }
        }
        else
        {
            bool bDestOnly = (addFlags & afOB_LAST) != 0;
            auto books = getApp().getOrderBookDB().getBooksByTakerPays(
                {uEndCurrency, uEndIssuer});
            WriteLog (lsTRACE, Pathfinder)
                << books.size() << " books found from this currency/issuer";

            for (auto const& book : books)
            {
                if (!currentPath.hasSeen (
                        xrpAccount(),
                        book->getCurrencyOut(),
                        book->getIssuerOut()) &&
                    !matchesOrigin (book->book().out) &&
                    (!bDestOnly ||
                     (book->getCurrencyOut() == mDstAmount.getCurrency())))
                {
                    STPath newPath(currentPath);

                    if (book->getCurrencyOut().isZero())
                    { // to XRP

                        // add the order book itself
                        newPath.emplace_back (STPathElement::typeCurrency,
                            xrpAccount(), xrpCurrency(), xrpAccount());

                        if (mDstAmount.getCurrency().isZero())
                        {
                            // destination is XRP, add account and path is
                            // complete
                            WriteLog (lsTRACE, Pathfinder)
                                << "complete path found bx: "
                                << currentPath.getJson(0);
                            add_unique_path (mCompletePaths, newPath);
                        }
                        else
                            incompletePaths.push_back (newPath);
                    }
                    else if (!currentPath.hasSeen(
                        book->getIssuerOut(),
                        book->getCurrencyOut(),
                        book->getIssuerOut()))
                    { // Don't want the book if we've already seen the issuer
                        // add the order book itself
                        newPath.emplace_back(
                            STPathElement::typeCurrency | STPathElement::typeIssuer,
                            xrpAccount(), book->getCurrencyOut(),
                            book->getIssuerOut());

                        if (book->getIssuerOut() == mDstAccountID &&
                            book->getCurrencyOut() == mDstAmount.getCurrency())
                        { // with the destination account, this path is complete
                            WriteLog (lsTRACE, Pathfinder)
                                << "complete path found ba: "
                                << currentPath.getJson(0);
                            add_unique_path (mCompletePaths, newPath);
                        }
                        else
                        { // add issuer's account, path still incomplete
                            incompletePaths.assembleAdd(newPath,
                                STPathElement(STPathElement::typeAccount,
                                              book->getIssuerOut(),
                                              book->getCurrencyOut(),
                                              book->getIssuerOut()));
                        }
                    }

                }
            }
        }
    }
}

Pathfinder::PathTable Pathfinder::mPathTable;

Pathfinder::PathType_t Pathfinder::makePath(char const *string)
{
    PathType_t ret;

    while (true)
    {
        switch (*string++)
        {
            case 's': // source
                ret.push_back(nt_SOURCE);
                break;

            case 'a': // accounts
                ret.push_back(nt_ACCOUNTS);
                break;

            case 'b': // books
                ret.push_back(nt_BOOKS);
                break;

            case 'x': // xrp book
                ret.push_back(nt_XRP_BOOK);
                break;

            case 'f': // book to final currency
                ret.push_back(nt_DEST_BOOK);
                break;

            case 'd':
                // Destination (with account, if required and not already
                // present).
                ret.push_back(nt_DESTINATION);
                break;

            case 0:
                return ret;
        }
    }
}

std::string Pathfinder::pathTypeToString(PathType_t const& type)
{
    std::string ret;

    for (auto const& node : type)
    {
        switch (node)
        {
            case nt_SOURCE:
                ret.append("s");
                break;
            case nt_ACCOUNTS:
                ret.append("a");
                break;
            case nt_BOOKS:
                ret.append("b");
                break;
            case nt_XRP_BOOK:
                ret.append("x");
                break;
            case nt_DEST_BOOK:
                ret.append("f");
                break;
            case nt_DESTINATION:
                ret.append("d");
                break;
        }
    }

    return ret;
}

void Pathfinder::fillPaths(PaymentType type, PathCostList const& costs)
{
    auto& list = mPathTable[type];
    for (auto& cost: costs)
        list.push_back ({cost.first, makePath(cost.second)});
}

// Costs:
// 0 = minimum to make some payments possible
// 1 = include trivial paths to make common cases work
// 4 = normal fast search level
// 7 = normal slow search level
// 10 = most agressive

void Pathfinder::initPathTable()
{
    // CAUTION: Do not include rules that build default paths
    fillPaths(
        pt_XRP_to_XRP, {});

    fillPaths(
        pt_XRP_to_nonXRP, {
            {1, "sfd"},   // source -> book -> gateway
            {3, "sfad"},  // source -> book -> account -> destination
            {5, "sfaad"}, // source -> book -> account -> account -> destination
            {6, "sbfd"},  // source -> book -> book -> destination
            {8, "sbafd"}, // source -> book -> account -> book -> destination
            {9, "sbfad"}, // source -> book -> book -> account -> destination
            {10, "sbafad"}
        });

    fillPaths(
        pt_XRP_to_nonXRP, {
            {1, "sfd"},
            {3, "sfad"},  // source -> book -> account -> destination
            {5, "sfaad"}, // source -> book -> account -> account -> destination
            {6, "sbfd"},  // source -> book -> book -> destination
            {8, "sbafd"}, // source -> book -> account -> book -> destination
            {9, "sbfad"}, // source -> book -> book -> account -> destination
            {10, "sbafad"}
        });

    fillPaths(
        pt_nonXRP_to_XRP, {
            {1, "sxd"},       // gateway buys XRP
            {2, "saxd"},      // source -> gateway -> book(XRP) -> dest
            {6, "saaxd"},
            {7, "sbxd"},
            {8, "sabxd"},
            {9, "sabaxd"}
        });

    // non-XRP to non-XRP (same currency)
    fillPaths(
        pt_nonXRP_to_same,  {
            {1, "sad"},     // source -> gateway -> destination
            {1, "sfd"},     // source -> book -> destination
            {4, "safd"},    // source -> gateway -> book -> destination
            {4, "sfad"},
            {5, "saad"},
            {5, "sbfd"},
            {6, "sxfad"},
            {6, "safad"},
            {6, "saxfd"},   // source -> gateway -> book to XRP -> book ->
                            // destination
            {6, "saxfad"},
            {6, "sabfd"},   // source -> gateway -> book -> book -> destination
            {6, "sabfd"},
            {7, "saaad"},
        });

    // non-XRP to non-XRP (different currency)
    fillPaths(
        pt_nonXRP_to_nonXRP, {
            {1, "sfad"},
            {1, "safd"},
            {3, "safad"},
            {4, "sxfd"},
            {5, "saxfd"},
            {5, "sxfad"},
            {6, "saxfad"},
            {6, "sabfd"},
            {7, "saafd"},
            {8, "saafad"},
            {9, "safaad"},
        });
}

STAmount
credit_limit (
    LedgerEntrySet& ledger, Account const& account,
    Account const& issuer, Currency const& currency)
{
    STAmount saLimit ({currency, account});

    auto sleRippleState = ledger.entryCache (ltRIPPLE_STATE,
        Ledger::getRippleStateIndex (account, issuer, currency));

    if (sleRippleState)
    {
        saLimit = sleRippleState->getFieldAmount (
            account < issuer ? sfLowLimit : sfHighLimit);
        saLimit.setIssuer (account);
    }

    assert (saLimit.getIssuer () == account);
    assert (saLimit.getCurrency () == currency);
    return saLimit;
}

STAmount
credit_balance (
    LedgerEntrySet& ledger, Account const& account,
    Account const& issuer, Currency const& currency)
{
    STAmount saBalance ({currency, account});

    auto sleRippleState = ledger.entryCache (ltRIPPLE_STATE,
        Ledger::getRippleStateIndex (account, issuer, currency));

    if (sleRippleState)
    {
        saBalance = sleRippleState->getFieldAmount (sfBalance);

        if (account < issuer)
            saBalance.negate ();

        saBalance.setIssuer (account);
    }

    assert (saBalance.getIssuer () == account);
    assert (saBalance.getCurrency () == currency);
    return saBalance;
}

} // ripple
