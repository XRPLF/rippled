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
#include <ripple/app/paths/Pathfinder.h>

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
    // return list of all orderbooks that want Issuer
    // return list of all orderbooks that want this issuer and currency
*/

/*
Test sending to XRP
Test XRP to XRP
Test offer in middle
Test XRP to USD
Test USD to EUR
*/

namespace {

// We sort possible paths by:
//    cost of path
//    length of path
//    width of path
//    correct currency at the end.

// Compare two PathRanks.  A better PathRank is lower, so the best are sorted to
// the beginning.
bool comparePathRank (
    Pathfinder::PathRank const& a, Pathfinder::PathRank const& b)
{
    // 1) Higher quality (lower cost) is better
    if (a.quality != b.quality)
        return a.quality < b.quality;

    // 2) More liquidity (higher volume) is better
    if (a.liquidity != b.liquidity)
        return a.liquidity > b.liquidity;

    // 3) Shorter paths are better
    if (a.length != b.length)
        return a.length < b.length;

    // 4) Tie breaker
    return a.index > b.index;
}

struct AccountCandidate
{
    int priority;
    Account account;

    static const int highPriority = 10000;
};

bool compareAccountCandidate (
    std::uint32_t seq,
    AccountCandidate const& first, AccountCandidate const& second)
{
    if (first.priority < second.priority)
        return false;

    if (first.account > second.account)
        return true;

    return (first.priority ^ seq) < (second.priority ^ seq);
}

typedef std::vector<AccountCandidate> AccountCandidates;

struct CostedPath
{
    int searchLevel;
    Pathfinder::PathType type;
};

typedef std::vector<CostedPath> CostedPathList;

typedef std::map<Pathfinder::PaymentType, CostedPathList> PathTable;

struct PathCost {
    int cost;
    char const* path;
};
typedef std::vector<PathCost> PathCostList;

static PathTable mPathTable;

std::string pathTypeToString (Pathfinder::PathType const& type)
{
    std::string ret;

    for (auto const& node : type)
    {
        switch (node)
        {
            case Pathfinder::nt_SOURCE:
                ret.append("s");
                break;
            case Pathfinder::nt_ACCOUNTS:
                ret.append("a");
                break;
            case Pathfinder::nt_BOOKS:
                ret.append("b");
                break;
            case Pathfinder::nt_XRP_BOOK:
                ret.append("x");
                break;
            case Pathfinder::nt_DEST_BOOK:
                ret.append("f");
                break;
            case Pathfinder::nt_DESTINATION:
                ret.append("d");
                break;
        }
    }

    return ret;
}

}  // namespace

Pathfinder::Pathfinder (
    RippleLineCache::ref cache,
    Account const& uSrcAccount,
    Account const& uDstAccount,
    Currency const& uSrcCurrency,
    Account const& uSrcIssuer,
    STAmount const& saDstAmount)
    :   mSrcAccount (uSrcAccount),
        mDstAccount (uDstAccount),
        mDstAmount (saDstAmount),
        mSrcCurrency (uSrcCurrency),
        mSrcIssuer (uSrcIssuer),
        mSrcAmount ({uSrcCurrency, uSrcIssuer}, 1u, 0, true),
        mLedger (cache->getLedger ()),
        mRLCache (cache)
{
}

Pathfinder::Pathfinder (
    RippleLineCache::ref cache,
    Account const& uSrcAccount,
    Account const& uDstAccount,
    Currency const& uSrcCurrency,
    STAmount const& saDstAmount)
    :   mSrcAccount (uSrcAccount),
        mDstAccount (uDstAccount),
        mDstAmount (saDstAmount),
        mSrcCurrency (uSrcCurrency),
        mSrcAmount ({uSrcCurrency, uSrcAccount}, 1u, 0, true),
        mLedger (cache->getLedger ()),
        mRLCache (cache)
{
}

Pathfinder::~Pathfinder()
{
}

bool Pathfinder::findPaths (int searchLevel)
{
    if (mDstAmount == zero)
    {
        // No need to send zero money.
        WriteLog (lsDEBUG, Pathfinder) << "Destination amount was zero.";
        mLedger.reset ();
        return false;

        // TODO(tom): why do we reset the ledger just in this case and the one
        // below - why don't we do it each time we return false?
    }

    if (mSrcAccount == mDstAccount &&
        mSrcCurrency == mDstAmount.getCurrency ())
    {
        // No need to send to same account with same currency.
        WriteLog (lsDEBUG, Pathfinder) << "Tried to send to same issuer";
        mLedger.reset ();
        return false;
    }

    m_loadEvent = getApp ().getJobQueue ().getLoadEvent (
        jtPATH_FIND, "FindPath");
    auto currencyIsXRP = isXRP (mSrcCurrency);

    bool useIssuerAccount
            = mSrcIssuer && !currencyIsXRP && !isXRP (*mSrcIssuer);
    auto& account = useIssuerAccount ? *mSrcIssuer : mSrcAccount;
    auto issuer = currencyIsXRP ? Account() : account;
    mSource = STPathElement (account, mSrcCurrency, issuer);
    auto issuerString = mSrcIssuer
            ? to_string (*mSrcIssuer) : std::string ("none");
    WriteLog (lsTRACE, Pathfinder)
            << "findPaths>"
            << " mSrcAccount=" << mSrcAccount
            << " mDstAccount=" << mDstAccount
            << " mDstAmount=" << mDstAmount.getFullText ()
            << " mSrcCurrency=" << mSrcCurrency
            << " mSrcIssuer=" << issuerString;

    if (!mLedger)
    {
        WriteLog (lsDEBUG, Pathfinder) << "findPaths< no ledger";
        return false;
    }

    bool bSrcXrp = isXRP (mSrcCurrency);
    bool bDstXrp = isXRP (mDstAmount.getCurrency());

    if (!mLedger->getSLEi (Ledger::getAccountRootIndex (mSrcAccount)))
    {
        // We can't even start without a source account.
        WriteLog (lsDEBUG, Pathfinder) << "invalid source account";
        return false;
    }

    if (!mLedger->getSLEi (Ledger::getAccountRootIndex (mDstAccount)))
    {
        // Can't find the destination account - we must be funding a new
        // account.
        if (!bDstXrp)
        {
            WriteLog (lsDEBUG, Pathfinder)
                    << "New account not being funded in XRP ";
            return false;
        }

        auto reserve = mLedger->getReserve (0);
        if (mDstAmount < reserve)
        {
            WriteLog (lsDEBUG, Pathfinder)
                    << "New account not getting enough funding: "
                    << mDstAmount << " < " << reserve;
            return false;
        }
    }

    // Now compute the payment type from the types of the source and destination
    // currencies.
    PaymentType paymentType;
    if (bSrcXrp && bDstXrp)
    {
        // XRP -> XRP
        WriteLog (lsDEBUG, Pathfinder) << "XRP to XRP payment";
        paymentType = pt_XRP_to_XRP;
    }
    else if (bSrcXrp)
    {
        // XRP -> non-XRP
        WriteLog (lsDEBUG, Pathfinder) << "XRP to non-XRP payment";
        paymentType = pt_XRP_to_nonXRP;
    }
    else if (bDstXrp)
    {
        // non-XRP -> XRP
        WriteLog (lsDEBUG, Pathfinder) << "non-XRP to XRP payment";
        paymentType = pt_nonXRP_to_XRP;
    }
    else if (mSrcCurrency == mDstAmount.getCurrency ())
    {
        // non-XRP -> non-XRP - Same currency
        WriteLog (lsDEBUG, Pathfinder) << "non-XRP to non-XRP - same currency";
        paymentType = pt_nonXRP_to_same;
    }
    else
    {
        // non-XRP to non-XRP - Different currency
        WriteLog (lsDEBUG, Pathfinder) << "non-XRP to non-XRP - cross currency";
        paymentType = pt_nonXRP_to_nonXRP;
    }

    // Now iterate over all paths for that paymentType.
    for (auto const& costedPath : mPathTable[paymentType])
    {
        // Only use paths with at most the current search level.
        if (costedPath.searchLevel <= searchLevel)
        {
            addPathsForType (costedPath.type);

            // TODO(tom): we might be missing other good paths with this
            // arbitrary cut off.
            if (mCompletePaths.size () > PATHFINDER_MAX_COMPLETE_PATHS)
                break;
        }
    }

    WriteLog (lsDEBUG, Pathfinder)
            << mCompletePaths.size () << " complete paths found";

    // Even if we find no paths, default paths may work, and we don't check them
    // currently.
    return true;
}

void Pathfinder::addPathsFromPreviousPathfinding (STPathSet& pathsOut)
{
    // Add any result paths that aren't in mCompletePaths.
    // TODO(tom): this is also quadratic in the size of the paths.
    for (auto const& path : pathsOut)
    {
        // make sure no paths were lost
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

            // TODO(tom): write a test that exercises this code path.
            if (!found)
                mCompletePaths.push_back (path);
        }
    }

    WriteLog (lsDEBUG, Pathfinder)
        << mCompletePaths.size () << " paths to filter";
}

TER Pathfinder::getPathLiquidity (
    STPath const& path,            // IN:  The path to check.
    STAmount const& minDstAmount,  // IN:  The minimum output this path must
                                   //      deliver to be worth keeping.
    STAmount& amountOut,           // OUT: The actual liquidity along the path.
    uint64_t& qualityOut) const    // OUT: The returned initial quality
{
    STPathSet pathSet;
    pathSet.push_back (path);

    path::RippleCalc::Input rcInput;
    rcInput.defaultPathsAllowed = false;

    LedgerEntrySet lesSandbox (mLedger, tapNONE);

    try
    {
        // Compute a path that provides at least the minimum liquidity.
        auto rc = path::RippleCalc::rippleCalculate (
            lesSandbox,
            mSrcAmount,
            minDstAmount,
            mDstAccount,
            mSrcAccount,
            pathSet,
            &rcInput);

        // If we can't get even the minimum liquidity requested, we're done.
        if (rc.result () != tesSUCCESS)
            return rc.result ();

        qualityOut = getRate (rc.actualAmountOut, rc.actualAmountIn);
        amountOut = rc.actualAmountOut;

        // Now try to compute the remaining liquidity.
        rcInput.partialPaymentAllowed = true;
        rc = path::RippleCalc::rippleCalculate (
            lesSandbox,
            mSrcAmount,
            mDstAmount - amountOut,
            mDstAccount,
            mSrcAccount,
            pathSet,
            &rcInput);

        // If we found further liquidity, add it into the result.
        if (rc.result () == tesSUCCESS)
            amountOut += rc.actualAmountOut;

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

namespace {

// Return the smallest amount of useful liquidity for a given amount, and the
// total number of paths we have to evaluate.
STAmount smallestUsefulAmount (STAmount const& amount, int maxPaths)
{
    return divide (amount, STAmount (maxPaths + 2), amount);
}

} // namespace

void Pathfinder::computePathRanks (int maxPaths)
{
    if (mCompletePaths.size () <= maxPaths)
        return;

    mRemainingAmount = mDstAmount;

    // Must subtract liquidity in default path from remaining amount.
    try
    {
        LedgerEntrySet lesSandbox (mLedger, tapNONE);

        path::RippleCalc::Input rcInput;
        rcInput.partialPaymentAllowed = true;
        auto rc = path::RippleCalc::rippleCalculate (
            lesSandbox,
            mSrcAmount,
            mDstAmount,
            mDstAccount,
            mSrcAccount,
            STPathSet(),
            &rcInput);

        if (rc.result () == tesSUCCESS)
        {
            WriteLog (lsDEBUG, Pathfinder)
                    << "Default path contributes: " << rc.actualAmountIn;
            mRemainingAmount -= rc.actualAmountOut;
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

    // Ignore paths that move only very small amounts.
    auto saMinDstAmount = smallestUsefulAmount (mDstAmount, maxPaths);

    // Get the PathRank for each path.
    for (int i = 0; i < mCompletePaths.size (); ++i)
    {
        auto const& currentPath = mCompletePaths[i];
        STAmount liquidity;
        uint64_t uQuality;
        auto const resultCode = getPathLiquidity (
            currentPath, saMinDstAmount, liquidity, uQuality);

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

            mPathRanks.push_back ({uQuality, currentPath.size (), liquidity, i});
        }
    }
    std::sort (mPathRanks.begin (), mPathRanks.end (), comparePathRank);
}

static bool isDefaultPath (STPath const& path)
{
    // TODO(tom): default paths can consist of more than just an account:
    // https://forum.ripple.com/viewtopic.php?f=2&t=8206&start=10#p57713
    //
    // JoelKatz writes:
    // So the test for whether a path is a default path is incorrect. I'm not
    // sure it's worth the complexity of fixing though. If we are going to fix
    // it, I'd suggest doing it this way:
    //
    // 1) Compute the default path, probably by using 'expandPath' to expand an
    // empty path.  2) Chop off the source and destination nodes.
    //
    // 3) In the pathfinding loop, if the source issuer is not the sender,
    // reject all paths that don't begin with the issuer's account node or match
    // the path we built at step 2.
    return path.size() == 1;
}

static STPath removeIssuer (STPath const& path)
{
    // This path starts with the issuer, which is already implied
    // so remove the head node
    STPath ret;

    for (auto it = path.begin() + 1; it != path.end(); ++it)
        ret.push_back (*it);

    return ret;
}

STPathSet Pathfinder::getBestPaths (
    int maxPaths,
    STPath& fullLiquidityPath,
    Account const& srcIssuer)
{
    assert (fullLiquidityPath.empty ());
    const bool issuerIsSender = isXRP (mSrcCurrency) || (srcIssuer == mSrcAccount);

    if (issuerIsSender && (mCompletePaths.size () <= maxPaths))
        return mCompletePaths;

    STPathSet bestPaths;

    // The best PathRanks are now at the start.  Pull off enough of them to
    // fill bestPaths, then look through the rest for the best individual
    // path that can satisfy the entire liquidity - if one exists.
    STAmount remaining = mRemainingAmount;
    for (auto& pathRank: mPathRanks)
    {
        auto iPathsLeft = maxPaths - bestPaths.size ();
        if (!(iPathsLeft > 0 || fullLiquidityPath.empty ()))
            break;

        auto& path = mCompletePaths[pathRank.index];
        assert (!path.empty ());
        if (path.empty ())
            continue;

        bool startsWithIssuer = false;
        if (! issuerIsSender)
        {
            if (path.front ().getAccountID() != srcIssuer)
                continue;
            if (isDefaultPath (path))
            {
                continue;
            }
            startsWithIssuer = true;
        }

        if (iPathsLeft > 1 ||
            (iPathsLeft > 0 && pathRank.liquidity >= remaining))
            // last path must fill
        {
            --iPathsLeft;
            remaining -= pathRank.liquidity;
            bestPaths.push_back (startsWithIssuer ? removeIssuer (path) : path);
        }
        else if (iPathsLeft == 0 &&
                 pathRank.liquidity >= mDstAmount &&
                 fullLiquidityPath.empty ())
        {
            // We found an extra path that can move the whole amount.
            fullLiquidityPath = (startsWithIssuer ? removeIssuer (path) : path);
            WriteLog (lsDEBUG, Pathfinder) <<
                "Found extra full path: " << fullLiquidityPath.getJson (0);
        }
        else
        {
            WriteLog (lsDEBUG, Pathfinder) <<
                "Skipping a non-filling path: " << path.getJson (0);
        }
    }

    if (remaining > zero)
    {
        assert (fullLiquidityPath.empty ());
        WriteLog (lsINFO, Pathfinder) <<
            "Paths could not send " << remaining << " of " << mDstAmount;
    }
    else
    {
        WriteLog (lsDEBUG, Pathfinder) <<
            "findPaths: RESULTS: " << bestPaths.getJson (0);
    }
    return bestPaths;
}

bool Pathfinder::issueMatchesOrigin (Issue const& issue)
{
    bool matchingCurrency = (issue.currency == mSrcCurrency);
    bool matchingAccount =
            isXRP (issue.currency) ||
            (mSrcIssuer && issue.account == mSrcIssuer) ||
            issue.account == mSrcAccount;

    return matchingCurrency && matchingAccount;
}

int Pathfinder::getPathsOut (
    Currency const& currency,
    Account const& account,
    bool isDstCurrency,
    Account const& dstAccount)
{
    Issue const issue (currency, account);

    auto it = mPathsOutCountMap.emplace (issue, 0);

    // If it was already present, return the stored number of paths
    if (!it.second)
        return it.first->second;

    auto sleAccount
            = mLedger->getSLEi (Ledger::getAccountRootIndex (account));

    if (!sleAccount)
        return 0;

    int aFlags = sleAccount->getFieldU32 (sfFlags);
    bool const bAuthRequired = (aFlags & lsfRequireAuth) != 0;
    bool const bFrozen = ((aFlags & lsfGlobalFreeze) != 0)
        && mLedger->enforceFreeze ();

    int count = 0;

    if (!bFrozen)
    {
        count = getApp ().getOrderBookDB ().getBookSize (issue);

        for (auto const& item : mRLCache->getRippleLines (account))
        {
            RippleState* rspEntry = (RippleState*) item.get ();

            if (currency != rspEntry->getLimit ().getCurrency ())
            {
            }
            else if (rspEntry->getBalance () <= zero &&
                     (!rspEntry->getLimitPeer ()
                      || -rspEntry->getBalance () >= rspEntry->getLimitPeer ()
                      ||  (bAuthRequired && !rspEntry->getAuth ())))
            {
            }
            else if (isDstCurrency &&
                     dstAccount == rspEntry->getAccountIDPeer ())
            {
                count += 10000; // count a path to the destination extra
            }
            else if (rspEntry->getNoRipplePeer ())
            {
                // This probably isn't a useful path out
            }
            else if (rspEntry->getFreezePeer () && mLedger->enforceFreeze ())
            {
                // Not a useful path out
            }
            else
            {
                ++count;
            }
        }
    }
    it.first->second = count;
    return count;
}

void Pathfinder::addLinks (
    STPathSet const& currentPaths,  // The paths to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags)
{
    WriteLog (lsDEBUG, Pathfinder)
        << "addLink< on " << currentPaths.size ()
        << " source(s), flags=" << addFlags;
    for (auto const& path: currentPaths)
        addLink (path, incompletePaths, addFlags);
}

STPathSet& Pathfinder::addPathsForType (PathType const& pathType)
{
    // See if the set of paths for this type already exists.
    auto it = mPaths.find (pathType);
    if (it != mPaths.end ())
        return it->second;

    // Otherwise, if the type has no nodes, return the empty path.
    if (pathType.empty ())
        return mPaths[pathType];

    // Otherwise, get the paths for the parent PathType by calling
    // addPathsForType recursively.
    PathType parentPathType = pathType;
    parentPathType.pop_back ();

    STPathSet const& parentPaths = addPathsForType (parentPathType);
    STPathSet& pathsOut = mPaths[pathType];

    WriteLog (lsDEBUG, Pathfinder)
        << "getPaths< adding onto '"
        << pathTypeToString (parentPathType) << "' to get '"
        << pathTypeToString (pathType) << "'";

    int initialSize = mCompletePaths.size ();

    // Add the last NodeType to the lists.
    auto nodeType = pathType.back ();
    switch (nodeType)
    {
    case nt_SOURCE:
        // Source must always be at the start, so pathsOut has to be empty.
        assert (pathsOut.empty ());
        pathsOut.push_back (STPath ());
        break;

    case nt_ACCOUNTS:
        addLinks (parentPaths, pathsOut, afADD_ACCOUNTS);
        break;

    case nt_BOOKS:
        addLinks (parentPaths, pathsOut, afADD_BOOKS);
        break;

    case nt_XRP_BOOK:
        addLinks (parentPaths, pathsOut, afADD_BOOKS | afOB_XRP);
        break;

    case nt_DEST_BOOK:
        addLinks (parentPaths, pathsOut, afADD_BOOKS | afOB_LAST);
        break;

    case nt_DESTINATION:
        // FIXME: What if a different issuer was specified on the
        // destination amount?
        // TODO(tom): what does this even mean?  Should it be a JIRA?
        addLinks (parentPaths, pathsOut, afADD_ACCOUNTS | afAC_LAST);
        break;
    }

    CondLog (mCompletePaths.size () != initialSize, lsDEBUG, Pathfinder)
        << (mCompletePaths.size () - initialSize)
        << " complete paths added";
    WriteLog (lsDEBUG, Pathfinder)
        << "getPaths> " << pathsOut.size () << " partial paths found";
    return pathsOut;
}

bool Pathfinder::isNoRipple (
    Account const& fromAccount,
    Account const& toAccount,
    Currency const& currency)
{
    SLE::pointer sleRipple = mLedger->getSLEi (
        Ledger::getRippleStateIndex (toAccount, fromAccount, currency));

    auto const flag ((toAccount > fromAccount)
                     ? lsfHighNoRipple : lsfLowNoRipple);

    return sleRipple && (sleRipple->getFieldU32 (sfFlags) & flag);
}

// Does this path end on an account-to-account link whose last account has
// set "no ripple" on the link?
bool Pathfinder::isNoRippleOut (STPath const& currentPath)
{
    // Must have at least one link.
    if (currentPath.empty ())
        return false;

    // Last link must be an account.
    STPathElement const& endElement = currentPath.back ();
    if (!(endElement.getNodeType () & STPathElement::typeAccount))
        return false;

    // If there's only one item in the path, return true if that item specifies
    // no ripple on the output. A path with no ripple on its output can't be
    // followed by a link with no ripple on its input.
    auto const& fromAccount = (currentPath.size () == 1)
        ? mSrcAccount
        : (currentPath.end () - 2)->getAccountID ();
    auto const& toAccount = endElement.getAccountID ();
    return isNoRipple (fromAccount, toAccount, endElement.getCurrency ());
}

void addUniquePath (STPathSet& pathSet, STPath const& path)
{
    // TODO(tom): building an STPathSet this way is quadratic in the size
    // of the STPathSet!
    for (auto const& p : pathSet)
    {
        if (p == path)
            return;
    }
    pathSet.push_back (path);
}

void Pathfinder::addLink (
    const STPath& currentPath,      // The path to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags)
{
    auto const& pathEnd = currentPath.empty() ? mSource : currentPath.back ();
    auto const& uEndCurrency = pathEnd.getCurrency ();
    auto const& uEndIssuer = pathEnd.getIssuerID ();
    auto const& uEndAccount = pathEnd.getAccountID ();
    bool const bOnXRP = uEndCurrency.isZero ();

    WriteLog (lsTRACE, Pathfinder) << "addLink< flags="
                                   << addFlags << " onXRP=" << bOnXRP;
    WriteLog (lsTRACE, Pathfinder) << currentPath.getJson (0);

    if (addFlags & afADD_ACCOUNTS)
    {
        // add accounts
        if (bOnXRP)
        {
            if (mDstAmount.isNative () && !currentPath.empty ())
            { // non-default path to XRP destination
                WriteLog (lsTRACE, Pathfinder)
                    << "complete path found ax: " << currentPath.getJson(0);
                addUniquePath (mCompletePaths, currentPath);
            }
        }
        else
        {
            // search for accounts to add
            auto sleEnd = mLedger->getSLEi(
                Ledger::getAccountRootIndex (uEndAccount));
            if (sleEnd)
            {
                bool const bRequireAuth (
                    sleEnd->getFieldU32 (sfFlags) & lsfRequireAuth);
                bool const bIsEndCurrency (
                    uEndCurrency == mDstAmount.getCurrency ());
                bool const bIsNoRippleOut (
                    isNoRippleOut (currentPath));
                bool const bDestOnly (
                    addFlags & afAC_LAST);

                auto& rippleLines (mRLCache->getRippleLines (uEndAccount));

                AccountCandidates candidates;
                candidates.reserve (rippleLines.size ());

                for (auto const& item : rippleLines)
                {
                    auto* rs = dynamic_cast<RippleState const *> (item.get ());
                    if (!rs)
                    {
                        WriteLog (lsERROR, Pathfinder)
                                << "Couldn't decipher RippleState";
                        continue;
                    }
                    auto const& acct = rs->getAccountIDPeer ();
                    bool const bToDestination = acct == mDstAccount;

                    if (bDestOnly && !bToDestination)
                    {
                        continue;
                    }

                    if ((uEndCurrency == rs->getLimit ().getCurrency ()) &&
                        !currentPath.hasSeen (acct, uEndCurrency, acct))
                    {
                        // path is for correct currency and has not been seen
                        if (rs->getBalance () <= zero
                            && (!rs->getLimitPeer ()
                                || -rs->getBalance () >= rs->getLimitPeer ()
                                || (bRequireAuth && !rs->getAuth ())))
                        {
                            // path has no credit
                        }
                        else if (bIsNoRippleOut && rs->getNoRipple ())
                        {
                            // Can't leave on this path
                        }
                        else if (bToDestination)
                        {
                            // destination is always worth trying
                            if (uEndCurrency == mDstAmount.getCurrency ())
                            {
                                // this is a complete path
                                if (!currentPath.empty ())
                                {
                                    WriteLog (lsTRACE, Pathfinder)
                                            << "complete path found ae: "
                                            << currentPath.getJson (0);
                                    addUniquePath
                                            (mCompletePaths, currentPath);
                                }
                            }
                            else if (!bDestOnly)
                            {
                                // this is a high-priority candidate
                                candidates.push_back (
                                    {AccountCandidate::highPriority, acct});
                            }
                        }
                        else if (acct == mSrcAccount)
                        {
                            // going back to the source is bad
                        }
                        else
                        {
                            // save this candidate
                            int out = getPathsOut (
                                uEndCurrency,
                                acct,
                                bIsEndCurrency,
                                mDstAccount);
                            if (out)
                                candidates.push_back ({out, acct});
                        }
                    }
                }

                if (!candidates.empty())
                {
                    std::sort (candidates.begin (), candidates.end (),
                        std::bind(compareAccountCandidate,
                                  mLedger->getLedgerSeq (),
                                  std::placeholders::_1,
                                  std::placeholders::_2));

                    int count = candidates.size ();
                    // allow more paths from source
                    if ((count > 10) && (uEndAccount != mSrcAccount))
                        count = 10;
                    else if (count > 50)
                        count = 50;

                    auto it = candidates.begin();
                    while (count-- != 0)
                    {
                        // Add accounts to incompletePaths
                        STPathElement pathElement (
                            STPathElement::typeAccount,
                            it->account,
                            uEndCurrency,
                            it->account);
                        incompletePaths.assembleAdd (currentPath, pathElement);
                        ++it;
                    }
                }

            }
            else
            {
                WriteLog (lsWARNING, Pathfinder)
                    << "Path ends on non-existent issuer";
            }
        }
    }
    if (addFlags & afADD_BOOKS)
    {
        // add order books
        if (addFlags & afOB_XRP)
        {
            // to XRP only
            if (!bOnXRP && getApp ().getOrderBookDB ().isBookToXRP (
                    {uEndCurrency, uEndIssuer}))
            {
                STPathElement pathElement(
                    STPathElement::typeCurrency,
                    xrpAccount (),
                    xrpCurrency (),
                    xrpAccount ());
                incompletePaths.assembleAdd (currentPath, pathElement);
            }
        }
        else
        {
            bool bDestOnly = (addFlags & afOB_LAST) != 0;
            auto books = getApp ().getOrderBookDB ().getBooksByTakerPays(
                {uEndCurrency, uEndIssuer});
            WriteLog (lsTRACE, Pathfinder)
                << books.size () << " books found from this currency/issuer";

            for (auto const& book : books)
            {
                if (!currentPath.hasSeen (
                        xrpAccount(),
                        book->getCurrencyOut (),
                        book->getIssuerOut ()) &&
                    !issueMatchesOrigin (book->book ().out) &&
                    (!bDestOnly ||
                     (book->getCurrencyOut () == mDstAmount.getCurrency ())))
                {
                    STPath newPath (currentPath);

                    if (book->getCurrencyOut().isZero())
                    { // to XRP

                        // add the order book itself
                        newPath.emplace_back (
                            STPathElement::typeCurrency,
                            xrpAccount (),
                            xrpCurrency (),
                            xrpAccount ());

                        if (mDstAmount.getCurrency ().isZero ())
                        {
                            // destination is XRP, add account and path is
                            // complete
                            WriteLog (lsTRACE, Pathfinder)
                                << "complete path found bx: "
                                << currentPath.getJson(0);
                            addUniquePath (mCompletePaths, newPath);
                        }
                        else
                            incompletePaths.push_back (newPath);
                    }
                    else if (!currentPath.hasSeen(
                        book->getIssuerOut (),
                        book->getCurrencyOut (),
                        book->getIssuerOut ()))
                    { // Don't want the book if we've already seen the issuer
                        // add the order book itself
                        newPath.emplace_back (
                            STPathElement::typeCurrency |
                            STPathElement::typeIssuer,
                            xrpAccount (),
                            book->getCurrencyOut (),
                            book->getIssuerOut ());

                        if (book->getIssuerOut () == mDstAccount &&
                            book->getCurrencyOut () == mDstAmount.getCurrency())
                        {
                            // with the destination account, this path is
                            // complete
                            WriteLog (lsTRACE, Pathfinder)
                                << "complete path found ba: "
                                << currentPath.getJson(0);
                            addUniquePath (mCompletePaths, newPath);
                        }
                        else
                        {
                            // add issuer's account, path still incomplete
                            incompletePaths.assembleAdd(newPath,
                                STPathElement (STPathElement::typeAccount,
                                               book->getIssuerOut (),
                                               book->getCurrencyOut (),
                                               book->getIssuerOut ()));
                        }
                    }

                }
            }
        }
    }
}

namespace {

Pathfinder::PathType makePath (char const *string)
{
    Pathfinder::PathType ret;

    while (true)
    {
        switch (*string++)
        {
            case 's': // source
                ret.push_back (Pathfinder::nt_SOURCE);
                break;

            case 'a': // accounts
                ret.push_back (Pathfinder::nt_ACCOUNTS);
                break;

            case 'b': // books
                ret.push_back (Pathfinder::nt_BOOKS);
                break;

            case 'x': // xrp book
                ret.push_back (Pathfinder::nt_XRP_BOOK);
                break;

            case 'f': // book to final currency
                ret.push_back (Pathfinder::nt_DEST_BOOK);
                break;

            case 'd':
                // Destination (with account, if required and not already
                // present).
                ret.push_back (Pathfinder::nt_DESTINATION);
                break;

            case 0:
                return ret;
        }
    }
}

void fillPaths (Pathfinder::PaymentType type, PathCostList const& costs)
{
    auto& list = mPathTable[type];
    assert (list.empty());
    for (auto& cost: costs)
        list.push_back ({cost.cost, makePath (cost.path)});
}

} // namespace


// Costs:
// 0 = minimum to make some payments possible
// 1 = include trivial paths to make common cases work
// 4 = normal fast search level
// 7 = normal slow search level
// 10 = most agressive

void Pathfinder::initPathTable ()
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
            {5, "sbfd"},
            {6, "saxfad"},
            {6, "sabfd"},
            {7, "saafd"},
            {8, "saafad"},
            {9, "safaad"},
        });
}

} // ripple
