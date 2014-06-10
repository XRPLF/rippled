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

#include <tuple>

#include <ripple/module/app/paths/Calculators.h>

namespace ripple {

SETUP_LOG (Pathfinder)

/*
we just need to find a succession of the highest quality paths there until we find enough width

Don't do branching within each path

We have a list of paths we are working on but how do we compare the ones that are terminating in a different currency?

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
static bool bQualityCmp (const path_LQ_t& a, const path_LQ_t& b)
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

typedef std::pair<int, uint160> candidate_t;
static bool candCmp (std::uint32_t seq, const candidate_t& first, const candidate_t& second)
{
    if (first.first < second.first)
        return false;

    if (first.first > second.first)
        return true;

    return (first.first ^ seq) < (second.first ^ seq);
}

Pathfinder::Pathfinder (RippleLineCache::ref cache,
                        const RippleAddress& uSrcAccountID, const RippleAddress& uDstAccountID,
                        const uint160& uSrcCurrencyID, const uint160& uSrcIssuerID, const STAmount& saDstAmount, bool& bValid)
    :   mSrcAccountID (uSrcAccountID.getAccountID ()),
        mDstAccountID (uDstAccountID.getAccountID ()),
        mDstAmount (saDstAmount),
        mSrcCurrencyID (uSrcCurrencyID),
        mSrcIssuerID (uSrcIssuerID),
        mSrcAmount (uSrcCurrencyID, uSrcIssuerID, 1u, 0, true),
        mLedger (cache->getLedger ()), mRLCache (cache)
{

    if ((mSrcAccountID == mDstAccountID && mSrcCurrencyID == mDstAmount.getCurrency ()) || mDstAmount == zero)
    {
        // no need to send to same account with same currency, must send non-zero
        bValid = false;
        mLedger.reset ();
        return;
    }

    bValid = true;

    m_loadEvent = getApp().getJobQueue ().getLoadEvent (jtPATH_FIND, "FindPath");

    bool bIssuer = mSrcCurrencyID.isNonZero() && mSrcIssuerID.isNonZero() && (mSrcIssuerID != mSrcAccountID);
    mSource = STPathElement(                       // Where does an empty path start?
        bIssuer ? mSrcIssuerID : mSrcAccountID,    // On the source account or issuer account
        mSrcCurrencyID,                            // In the source currency
        mSrcCurrencyID.isZero() ? uint160() : (bIssuer ? mSrcIssuerID : mSrcAccountID));

}

bool Pathfinder::findPaths (int iLevel, const unsigned int iMaxPaths, STPathSet& pathsOut, STPath& extraPath)
{ // pathsOut contains only non-default paths without source or destiation
// On input, pathsOut contains any paths you want to ensure are included if still good

    WriteLog (lsTRACE, Pathfinder) << "findPaths>"
        " mSrcAccountID=" << RippleAddress::createHumanAccountID (mSrcAccountID) <<
        " mDstAccountID=" << RippleAddress::createHumanAccountID (mDstAccountID) <<
        " mDstAmount=" << mDstAmount.getFullText () <<
        " mSrcCurrencyID=" << STAmount::createHumanCurrency (mSrcCurrencyID) <<
        " mSrcIssuerID=" << RippleAddress::createHumanAccountID (mSrcIssuerID);

    if (!mLedger)
    {
        WriteLog (lsDEBUG, Pathfinder) << "findPaths< no ledger";

        return false;
    }

    bool bSrcXrp       = mSrcCurrencyID.isZero();
    bool bDstXrp       = mDstAmount.getCurrency().isZero();

    SLE::pointer sleSrc = mLedger->getSLEi(Ledger::getAccountRootIndex(mSrcAccountID));
    if (!sleSrc)
        return false;

    SLE::pointer sleDest = mLedger->getSLEi(Ledger::getAccountRootIndex(mDstAccountID));
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

    BOOST_FOREACH(CostedPath_t const& costedPath, mPathTable[paymentType])
    {
       if (costedPath.first <= iLevel)
       {
           getPaths(costedPath.second);
       }
    }

    WriteLog (lsDEBUG, Pathfinder) << mCompletePaths.size() << " complete paths found";

    BOOST_FOREACH(const STPath& path, pathsOut)
    { // make sure no paths were lost
        bool found = false;
        if (!path.isEmpty ())
        {
            BOOST_FOREACH(const STPath& ePath, mCompletePaths)
            {
                if (ePath == path)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                mCompletePaths.addPath(path);
        }
    }

    WriteLog (lsDEBUG, Pathfinder) << mCompletePaths.size() << " paths to filter";

    if (mCompletePaths.size() > iMaxPaths)
        pathsOut = filterPaths(iMaxPaths, extraPath);
    else
        pathsOut = mCompletePaths;

    return true; // Even if we find no paths, default paths may work, and we don't check them currently
}

STPathSet Pathfinder::filterPaths(int iMaxPaths, STPath& extraPath)
{
    if (mCompletePaths.size() <= iMaxPaths)
        return mCompletePaths;

    STAmount remaining = mDstAmount;

    // must subtract liquidity in default path from remaining amount
    try
    {
        STAmount saMaxAmountAct, saDstAmountAct;
        PathState::List pathStateList;
        LedgerEntrySet lesSandbox (mLedger, tapNONE);

        TER result = path::rippleCalculate (
                 lesSandbox,
                 saMaxAmountAct,
                 saDstAmountAct,
                 pathStateList,
                 mSrcAmount,
                 mDstAmount,
                 mDstAccountID,
                 mSrcAccountID,
                 STPathSet (),
                 true,       // allow partial payment
                 false,
                 false,      // don't suppress default paths, that's the point
                 true);

        if (tesSUCCESS == result)
        {
            WriteLog (lsDEBUG, Pathfinder) << "Default path contributes: " << saDstAmountAct;
            remaining -= saDstAmountAct;
        }
        else
        {
            WriteLog (lsDEBUG, Pathfinder) << "Default path fails: " << transToken (result);
        }
    }
    catch (...)
    {
        WriteLog (lsDEBUG, Pathfinder) << "Default path causes exception";
    }

    std::vector<path_LQ_t> vMap;

    // Ignore paths that move only very small amounts
    STAmount saMinDstAmount = STAmount::divide(mDstAmount, STAmount(iMaxPaths + 2), mDstAmount);

    // Build map of quality to entry.
    for (int i = mCompletePaths.size (); i--;)
    {
        STAmount    saMaxAmountAct;
        STAmount    saDstAmountAct;
        PathState::List pathStateList;
        STPathSet   spsPaths;
        STPath&     spCurrent   = mCompletePaths[i];

        spsPaths.addPath (spCurrent);               // Just checking the current path.

        TER         resultCode;

        try
        {
            LedgerEntrySet lesSandbox (mLedger, tapNONE);

            resultCode   = path::rippleCalculate (
                              lesSandbox,
                              saMaxAmountAct,     // --> computed input
                              saDstAmountAct,     // --> computed output
                              pathStateList,
                              mSrcAmount,         // --> amount to send max.
                              mDstAmount,         // --> amount to deliver.
                              mDstAccountID,
                              mSrcAccountID,
                              spsPaths,
                              true,               // --> bPartialPayment: Allow, it might contribute.
                              false,              // --> bLimitQuality: Assume normal transaction.
                              true,               // --> bNoRippleDirect: Providing the only path.
                              true);              // --> bStandAlone: Don't need to delete unfundeds.
        }
        catch (const std::exception& e)
        {
            WriteLog (lsINFO, Pathfinder) << "findPaths: Caught throw: " << e.what ();

            resultCode   = tefEXCEPTION;
        }

        if (resultCode != tesSUCCESS)
        {
            WriteLog (lsDEBUG, Pathfinder) <<
                "findPaths: dropping: " << transToken (resultCode) <<
                ": " << spCurrent.getJson (0);
        }
        else if (saDstAmountAct < saMinDstAmount)
        {
            WriteLog (lsDEBUG, Pathfinder) <<
                "findPaths: dropping: outputs " << saDstAmountAct <<
                ": %s" << spCurrent.getJson (0);
        }
        else
        {
            std::uint64_t  uQuality (
                STAmount::getRate (saDstAmountAct, saMaxAmountAct));

            WriteLog (lsDEBUG, Pathfinder) <<
                "findPaths: quality: " << uQuality <<
                ": " << spCurrent.getJson (0);

            vMap.push_back (path_LQ_t (
                uQuality, spCurrent.mPath.size (), saDstAmountAct, i));
        }
    }

    STPathSet spsDst;

    if (vMap.size())
    {
        std::sort (vMap.begin (), vMap.end (), bQualityCmp); // Lower is better and should be first.


        for (int i = 0, iPathsLeft = iMaxPaths;
            ((iPathsLeft > 0) || (extraPath.size() == 0)) && (i < vMap.size ()); ++i)
        {
            path_LQ_t& lqt = vMap[i];

            if ((iPathsLeft > 1) || ((iPathsLeft > 0) && (std::get<2> (lqt) >= remaining)))
            {
                // last path must fill
                --iPathsLeft;
                remaining -= std::get<2> (lqt);
                spsDst.addPath (mCompletePaths[std::get<3> (lqt)]);
            }
            else if ((iPathsLeft == 0) && (std::get<2>(lqt) >= mDstAmount) && (extraPath.size() == 0))
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

boost::unordered_set<uint160> usAccountSourceCurrencies (
        const RippleAddress& raAccountID, RippleLineCache::ref lrCache,
        bool includeXRP)
{
    boost::unordered_set<uint160>   usCurrencies;

    // YYY Only bother if they are above reserve
    if (includeXRP)
        usCurrencies.insert (uint160 (XRP_CURRENCY));

    // List of ripple lines.
    AccountItems& rippleLines (lrCache->getRippleLines (raAccountID.getAccountID ()));

    BOOST_FOREACH (AccountItem::ref item, rippleLines.getItems ())
    {
        RippleState*    rspEntry    = (RippleState*) item.get ();
        const STAmount& saBalance   = rspEntry->getBalance ();

        // Filter out non
        if (saBalance > zero                             // Have IOUs to send.
                || (rspEntry->getLimitPeer ()                       // Peer extends credit.
                    && ((-saBalance) < rspEntry->getLimitPeer ()))) // Credit left.
        {
            usCurrencies.insert (saBalance.getCurrency ());
        }
    }

    usCurrencies.erase (CURRENCY_BAD);
    return usCurrencies;
}

boost::unordered_set<uint160> usAccountDestCurrencies (
        const RippleAddress& raAccountID,
        RippleLineCache::ref lrCache,
        bool includeXRP)
{
    boost::unordered_set<uint160>   usCurrencies;

    if (includeXRP)
        usCurrencies.insert (uint160 (XRP_CURRENCY)); // Even if account doesn't exist

    // List of ripple lines.
    AccountItems& rippleLines (lrCache->getRippleLines (raAccountID.getAccountID ()));

    BOOST_FOREACH (AccountItem::ref item, rippleLines.getItems ())
    {
        RippleState*    rspEntry    = (RippleState*) item.get ();
        const STAmount& saBalance   = rspEntry->getBalance ();

        if (saBalance < rspEntry->getLimit ())                  // Can take more
            usCurrencies.insert (saBalance.getCurrency ());
    }

    usCurrencies.erase (CURRENCY_BAD);
    return usCurrencies;
}

bool Pathfinder::matchesOrigin (const uint160& currency, const uint160& issuer)
{
    if (currency != mSrcCurrencyID)
        return false;

    if (currency.isZero())
        return true;

    return (issuer == mSrcIssuerID) || (issuer == mSrcAccountID);
}

// VFALCO TODO Use RippleCurrency, RippleAccount, et. al. in argument list here
int Pathfinder::getPathsOut (RippleCurrency const& currencyID, const uint160& accountID,
                             bool isDstCurrency, const uint160& dstAccount)
{
    // VFALCO TODO Use RippleAsset here
    std::pair<const uint160&, const uint160&> accountCurrency (currencyID, accountID);
    // VFALCO TODO Use RippleAsset here
    ripple::unordered_map<std::pair<uint160, uint160>, int>::iterator it = mPOMap.find (accountCurrency);

    if (it != mPOMap.end ())
        return it->second;

    SLE::pointer sleAccount = mLedger->getSLEi(Ledger::getAccountRootIndex(accountID));
    if (!sleAccount)
    {
        mPOMap[accountCurrency] = 0;
        return 0;
    }

    int aFlags = sleAccount->getFieldU32(sfFlags);
    bool const bAuthRequired = (aFlags & lsfRequireAuth) != 0;

    int count = 0;
    AccountItems& rippleLines (mRLCache->getRippleLines (accountID));

    BOOST_FOREACH (AccountItem::ref item, rippleLines.getItems ())
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
        else
            ++count;
    }
    mPOMap[accountCurrency] = count;
    return count;
}

void Pathfinder::addLink(
    const STPathSet& currentPaths,  // The paths to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags)
{
    WriteLog (lsDEBUG, Pathfinder) << "addLink< on " << currentPaths.size() << " source(s), flags=" << addFlags;
    BOOST_FOREACH(const STPath& path, currentPaths)
    {
        addLink(path, incompletePaths, addFlags);
    }
}

STPathSet& Pathfinder::getPaths(PathType_t const& type, bool addComplete)
{
    std::map< PathType_t, STPathSet >::iterator it = mPaths.find(type);

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

        { // source is an empty path
            assert(pathsOut.isEmpty());
            pathsOut.addPath(STPath());
        }
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
            // FIXME: What if a different issuer was specified on the destination amount
            addLink(pathsIn, pathsOut, afADD_ACCOUNTS | afAC_LAST);
            break;

    }

    CondLog (mCompletePaths.size() != cp, lsDEBUG, Pathfinder)
        << (mCompletePaths.size() - cp)
        << " complete paths added";
    WriteLog (lsDEBUG, Pathfinder) << "getPaths> " << pathsOut.size() << " partial paths found";
    return pathsOut;
}

bool Pathfinder::isNoRipple (const uint160& setByID, const uint160& setOnID, const uint160& currencyID)
{
    SLE::pointer sleRipple = mLedger->getSLEi (
        Ledger::getRippleStateIndex (setByID, setOnID, currencyID));

    auto const flag ((setByID > setOnID) ? lsfHighNoRipple : lsfLowNoRipple);

    return sleRipple && (sleRipple->getFieldU32 (sfFlags) & flag);
}

// Does this path end on an account-to-account link whose last account
// has set no ripple on the link?
bool Pathfinder::isNoRippleOut (const STPath& currentPath)
{
    // Must have at least one link
    if (currentPath.size() == 0)
        return false;

    // Last link must be an account
    STPathElement const& endElement = *(currentPath.end() - 1);
    if (!(endElement.getNodeType() & STPathElement::typeAccount))
        return false;

    // What account are we leaving?
    uint160 const& fromAccount =
        (currentPath.size() == 1) ? mSrcAccountID : (currentPath.end() - 2)->mAccountID;

    return isNoRipple (endElement.mAccountID, fromAccount, endElement.mCurrencyID);
}

void Pathfinder::addLink(
    const STPath& currentPath,      // The path to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags)
{
    STPathElement const& pathEnd   = currentPath.isEmpty() ? mSource : currentPath.mPath.back ();
    uint160 const& uEndCurrency    = pathEnd.mCurrencyID;
    uint160 const& uEndIssuer      = pathEnd.mIssuerID;
    uint160 const& uEndAccount     = pathEnd.mAccountID;
    bool const bOnXRP              = uEndCurrency.isZero();

    WriteLog (lsTRACE, Pathfinder) << "addLink< flags=" << addFlags << " onXRP=" << bOnXRP;
    WriteLog (lsTRACE, Pathfinder) << currentPath.getJson(0);

    if (addFlags & afADD_ACCOUNTS)
    { // add accounts
        if (bOnXRP)
        {
            if (mDstAmount.isNative() && !currentPath.isEmpty())
            { // non-default path to XRP destination
                WriteLog (lsTRACE, Pathfinder) << "complete path found ax: " << currentPath.getJson(0);
                mCompletePaths.addUniquePath(currentPath);
            }
        }
        else
        { // search for accounts to add
            SLE::pointer sleEnd = mLedger->getSLEi(Ledger::getAccountRootIndex(uEndAccount));
            if (sleEnd)
            {
                bool const bRequireAuth (sleEnd->getFieldU32(sfFlags) & lsfRequireAuth);
                bool const bIsEndCurrency (uEndCurrency == mDstAmount.getCurrency());
                bool const bIsNoRippleOut (isNoRippleOut (currentPath));

                AccountItems& rippleLines (mRLCache->getRippleLines(uEndAccount));

                std::vector< std::pair<int, uint160> > candidates;
                candidates.reserve(rippleLines.getItems().size());

                for(auto item : rippleLines.getItems())
                {
                    RippleState const& rspEntry = * reinterpret_cast<RippleState const *>(item.get());
                    uint160 const& acctID = rspEntry.getAccountIDPeer();

                    if ((uEndCurrency == rspEntry.getLimit().getCurrency()) &&
                        !currentPath.hasSeen(acctID, uEndCurrency, acctID))
                    { // path is for correct currency and has not been seen
                        if (rspEntry.getBalance() <= zero
                            && (!rspEntry.getLimitPeer()
                                || -rspEntry.getBalance() >= rspEntry.getLimitPeer()
                                || (bRequireAuth && !rspEntry.getAuth())))
                        {
                            // path has no credit
                        }
                        else if (bIsNoRippleOut && rspEntry.getNoRipple())
                        {
                            // Can't leave on this path
                        }
                        else if (acctID == mDstAccountID)
                        { // destination is always worth trying
                            if (uEndCurrency == mDstAmount.getCurrency())
                            { // this is a complete path
                                if (!currentPath.isEmpty())
                                {
                                    WriteLog (lsTRACE, Pathfinder) << "complete path found ae: " << currentPath.getJson(0);
                                    mCompletePaths.addUniquePath(currentPath);
                                }
                            }
                            else if ((addFlags & afAC_LAST) == 0)
                            { // this is a high-priority candidate
                                candidates.push_back(std::make_pair(100000, acctID));
                            }
                        }
                        else if (acctID == mSrcAccountID)
                        {
                            // going back to the source is bad
                        }
                        else if ((addFlags & afAC_LAST) == 0)
                        { // save this candidate
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
                    if ((count > 10) && (uEndAccount != mSrcAccountID)) // allow more paths from source
                        count = 10;
                    else if (count > 50)
                        count = 50;

                    std::vector< std::pair<int, uint160> >::const_iterator it = candidates.begin();
                    while (count-- != 0)
                    { // Add accounts to incompletePaths
                        incompletePaths.assembleAdd(currentPath, STPathElement(STPathElement::typeAccount, it->second, uEndCurrency, it->second));
                        ++it;
                    }
                }

            }
            else
            {
                WriteLog(lsWARNING, Pathfinder) << "Path ends on non-existent issuer";
            }
        }
    }
    if (addFlags & afADD_BOOKS)
    { // add order books
        if (addFlags & afOB_XRP)
        { // to XRP only
            if (!bOnXRP && getApp().getOrderBookDB().isBookToXRP(uEndIssuer, uEndCurrency))
            {
                STPathElement pathElement(
                    STPathElement::typeCurrency,
                    XRP_ACCOUNT, XRP_CURRENCY, XRP_ACCOUNT);
                incompletePaths.assembleAdd(currentPath, pathElement);
            }
        }
        else
        {
            bool bDestOnly = (addFlags & afOB_LAST) != 0;
            std::vector<OrderBook::pointer> books;
            getApp().getOrderBookDB().getBooksByTakerPays(uEndIssuer, uEndCurrency, books);
            WriteLog (lsTRACE, Pathfinder) << books.size() << " books found from this currency/issuer";
            BOOST_FOREACH(OrderBook::ref book, books)
            {
                if (!currentPath.hasSeen (
                        XRP_ACCOUNT,
                        book->getCurrencyOut(),
                        book->getIssuerOut()) &&
                    !matchesOrigin(
                        book->getCurrencyOut(),
                        book->getIssuerOut()) &&
                    (!bDestOnly ||
                     (book->getCurrencyOut() == mDstAmount.getCurrency())))
                {
                    STPath newPath(currentPath);

                    if (book->getCurrencyOut().isZero())
                    { // to XRP

                        // add the order book itself
                        newPath.addElement(STPathElement(
                            STPathElement::typeCurrency,
                            XRP_ACCOUNT, XRP_CURRENCY, XRP_ACCOUNT));

                        if (mDstAmount.getCurrency().isZero())
                        { // destination is XRP, add account and path is complete
                            WriteLog (lsTRACE, Pathfinder) << "complete path found bx: " << currentPath.getJson(0);
                            mCompletePaths.addUniquePath(newPath);
                        }
                        else
                            incompletePaths.addPath(newPath);
                    }
                    else if (!currentPath.hasSeen(book->getIssuerOut(), book->getCurrencyOut(), book->getIssuerOut()))
                    { // Don't want the book if we've already seen the issuer
                        // add the order book itself
                        newPath.addElement(STPathElement(STPathElement::typeCurrency | STPathElement::typeIssuer,
                            XRP_ACCOUNT, book->getCurrencyOut(), book->getIssuerOut()));

                        if ((book->getIssuerOut() == mDstAccountID) && book->getCurrencyOut() == mDstAmount.getCurrency())
                        { // with the destination account, this path is complete
                            WriteLog (lsTRACE, Pathfinder) << "complete path found ba: " << currentPath.getJson(0);
                            mCompletePaths.addUniquePath(newPath);
                        }
                        else
                        { // add issuer's account, path still incomplete
                            incompletePaths.assembleAdd(newPath,
                                STPathElement(STPathElement::typeAccount,
                                    book->getIssuerOut(), book->getCurrencyOut(), book->getIssuerOut()));
                        }
                    }

                }
            }
        }
    }
}

std::map<Pathfinder::PaymentType, Pathfinder::CostedPathList_t> Pathfinder::mPathTable;

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

            case 'd': // destination (with account, if required and not already present)
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

    BOOST_FOREACH(NodeType const& node, type)
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

// Costs:
// 0 = minimum to make some payments possible
// 1 = include trivial paths to make common cases work
// 4 = normal fast search level
// 7 = normal slow search level
// 10 = most agressive

void Pathfinder::initPathTable()
{ // CAUTION: Do not include rules that build default paths
    { // XRP to XRP
        // do not remove this - it's necessary to build the table.
        /*CostedPathList_t& list =*/ mPathTable[pt_XRP_to_XRP];
//        list.push_back(CostedPath_t(8, makePath("sbxd")));   // source -> book -> book_to_XRP -> destination
//        list.push_back(CostedPath_t(9, makePath("sbaxd")));  // source -> book -> gateway -> to_XRP ->destination
    }

    { // XRP to non-XRP
        CostedPathList_t& list = mPathTable[pt_XRP_to_nonXRP];

        list.push_back(CostedPath_t(1,  makePath("sfd")));       // source -> book -> gateway
        list.push_back(CostedPath_t(3,  makePath("sfad")));      // source -> book -> account -> destination
        list.push_back(CostedPath_t(5,  makePath("sfaad")));     // source -> book -> account -> account -> destination
        list.push_back(CostedPath_t(6,  makePath("sbfd")));      // source -> book -> book -> destination
        list.push_back(CostedPath_t(8,  makePath("sbafd")));     // source -> book -> account -> book -> destination
        list.push_back(CostedPath_t(9,  makePath("sbfad")));     // source -> book -> book -> account -> destination
        list.push_back(CostedPath_t(10, makePath("sbafad")));
    }

    { // non-XRP to XRP
        CostedPathList_t& list = mPathTable[pt_nonXRP_to_XRP];

        list.push_back(CostedPath_t(1, makePath("sxd")));              // gateway buys XRP
        list.push_back(CostedPath_t(2, makePath("saxd")));             // source -> gateway -> book(XRP) -> dest
        list.push_back(CostedPath_t(6, makePath("saaxd")));
        list.push_back(CostedPath_t(7, makePath("sbxd")));
        list.push_back(CostedPath_t(8, makePath("sabxd")));
        list.push_back(CostedPath_t(9, makePath("sabaxd")));
    }

    { // non-XRP to non-XRP (same currency)
        CostedPathList_t& list = mPathTable[pt_nonXRP_to_same];

        list.push_back(CostedPath_t(1, makePath("sad")));               // source -> gateway -> destination
        list.push_back(CostedPath_t(1, makePath("sfd")));               // source -> book -> destination
        list.push_back(CostedPath_t(4, makePath("safd")));              // source -> gateway -> book -> destination
        list.push_back(CostedPath_t(4, makePath("sfad")));
        list.push_back(CostedPath_t(5, makePath("saad")));
        list.push_back(CostedPath_t(5, makePath("sbfd")));
        list.push_back(CostedPath_t(6, makePath("sxfad")));
        list.push_back(CostedPath_t(6, makePath("safad")));
        list.push_back(CostedPath_t(6, makePath("saxfd")));             // source -> gateway -> book to XRP -> book -> destination
        list.push_back(CostedPath_t(6, makePath("saxfad")));
        list.push_back(CostedPath_t(7, makePath("saaad")));
    }

    { // non-XRP to non-XRP (different currency)
        CostedPathList_t& list = mPathTable[pt_nonXRP_to_nonXRP];

        list.push_back(CostedPath_t(1, makePath("sfad")));
        list.push_back(CostedPath_t(1, makePath("safd")));
        list.push_back(CostedPath_t(3, makePath("safad")));
        list.push_back(CostedPath_t(4, makePath("sxfd")));
        list.push_back(CostedPath_t(5, makePath("saxfd")));
        list.push_back(CostedPath_t(5, makePath("sxfad")));
        list.push_back(CostedPath_t(6, makePath("saxfad")));
        list.push_back(CostedPath_t(6, makePath("sbfd")));
        list.push_back(CostedPath_t(7, makePath("saafd")));
        list.push_back(CostedPath_t(8, makePath("saafad")));
        list.push_back(CostedPath_t(9, makePath("safaad")));
    }

}

} // ripple
