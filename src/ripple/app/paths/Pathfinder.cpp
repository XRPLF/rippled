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

#include <ripple/app/ledger/OrderBookDB.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/paths/Pathfinder.h>
#include <ripple/app/paths/RippleCalc.h>
#include <ripple/app/paths/RippleLineCache.h>
#include <ripple/app/paths/Tuning.h>
#include <ripple/basics/Log.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/json/to_string.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <tuple>

/*

Core Pathfinding Engine

The pathfinding request is identified by category, XRP to XRP, XRP to
non-XRP, non-XRP to XRP, same currency non-XRP to non-XRP, cross-currency
non-XRP to non-XRP.  For each category, there is a table of paths that the
pathfinder searches for.  Complete paths are collected.

Each complete path is then rated and sorted. Paths with no or trivial
liquidity are dropped.  Otherwise, paths are sorted based on quality,
liquidity, and path length.

Path slots are filled in quality (ratio of out to in) order, with the
exception that the last path must have enough liquidity to complete the
payment (assuming no liquidity overlap).  In addition, if no selected path
is capable of providing enough liquidity to complete the payment by itself,
an extra "covering" path is returned.

The selected paths are then tested to determine if they can complete the
payment and, if so, at what cost.  If they fail and a covering path was
found, the test is repeated with the covering path.  If this succeeds, the
final paths and the estimated cost are returned.

The engine permits the search depth to be selected and the paths table
includes the depth at which each path type is found.  A search depth of zero
causes no searching to be done.  Extra paths can also be injected, and this
should be used to preserve previously-found paths across invokations for the
same path request (particularly if the search depth may change).

*/

namespace ripple {

namespace {

struct AccountCandidate
{
    int priority;
    AccountID account;

    static const int highPriority = 10000;
};

bool
compareAccountCandidate(
    std::uint32_t seq,
    AccountCandidate const& first,
    AccountCandidate const& second)
{
    if (first.priority < second.priority)
        return false;

    if (first.account > second.account)
        return true;

    return (first.priority ^ seq) < (second.priority ^ seq);
}

using AccountCandidates = std::vector<AccountCandidate>;

struct CostedPath
{
    int searchLevel;
    Pathfinder::PathType type;
};

using CostedPathList = std::vector<CostedPath>;

using PathTable = std::map<Pathfinder::PaymentType, CostedPathList>;

struct PathCost
{
    int cost;
    char const* path;
};
using PathCostList = std::vector<PathCost>;

static PathTable mPathTable;

std::string
pathTypeToString(Pathfinder::PathType const& type)
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

Pathfinder::Pathfinder(
    std::shared_ptr<RippleLineCache> const& cache,
    AccountID const& uSrcAccount,
    AccountID const& uDstAccount,
    Currency const& uSrcCurrency,
    boost::optional<AccountID> const& uSrcIssuer,
    STAmount const& saDstAmount,
    boost::optional<STAmount> const& srcAmount,
    Application& app)
    : mSrcAccount(uSrcAccount)
    , mDstAccount(uDstAccount)
    , mEffectiveDst(
          isXRP(saDstAmount.getIssuer()) ? uDstAccount
                                         : saDstAmount.getIssuer())
    , mDstAmount(saDstAmount)
    , mSrcCurrency(uSrcCurrency)
    , mSrcIssuer(uSrcIssuer)
    , mSrcAmount(srcAmount.value_or(STAmount(
          {uSrcCurrency,
           uSrcIssuer.value_or(
               isXRP(uSrcCurrency) ? xrpAccount() : uSrcAccount)},
          1u,
          0,
          true)))
    , convert_all_(
          mDstAmount ==
          STAmount(
              mDstAmount.issue(),
              STAmount::cMaxValue,
              STAmount::cMaxOffset))
    , mLedger(cache->getLedger())
    , mRLCache(cache)
    , app_(app)
    , j_(app.journal("Pathfinder"))
{
    assert(!uSrcIssuer || isXRP(uSrcCurrency) == isXRP(uSrcIssuer.get()));
}

bool
Pathfinder::findPaths(int searchLevel)
{
    if (mDstAmount == beast::zero)
    {
        // No need to send zero money.
        JLOG(j_.debug()) << "Destination amount was zero.";
        mLedger.reset();
        return false;

        // TODO(tom): why do we reset the ledger just in this case and the one
        // below - why don't we do it each time we return false?
    }

    if (mSrcAccount == mDstAccount && mDstAccount == mEffectiveDst &&
        mSrcCurrency == mDstAmount.getCurrency())
    {
        // No need to send to same account with same currency.
        JLOG(j_.debug()) << "Tried to send to same issuer";
        mLedger.reset();
        return false;
    }

    if (mSrcAccount == mEffectiveDst &&
        mSrcCurrency == mDstAmount.getCurrency())
    {
        // Default path might work, but any path would loop
        return true;
    }

    m_loadEvent = app_.getJobQueue().makeLoadEvent(jtPATH_FIND, "FindPath");
    auto currencyIsXRP = isXRP(mSrcCurrency);

    bool useIssuerAccount = mSrcIssuer && !currencyIsXRP && !isXRP(*mSrcIssuer);
    auto& account = useIssuerAccount ? *mSrcIssuer : mSrcAccount;
    auto issuer = currencyIsXRP ? AccountID() : account;
    mSource = STPathElement(account, mSrcCurrency, issuer);
    auto issuerString =
        mSrcIssuer ? to_string(*mSrcIssuer) : std::string("none");
    JLOG(j_.trace()) << "findPaths>"
                     << " mSrcAccount=" << mSrcAccount
                     << " mDstAccount=" << mDstAccount
                     << " mDstAmount=" << mDstAmount.getFullText()
                     << " mSrcCurrency=" << mSrcCurrency
                     << " mSrcIssuer=" << issuerString;

    if (!mLedger)
    {
        JLOG(j_.debug()) << "findPaths< no ledger";
        return false;
    }

    bool bSrcXrp = isXRP(mSrcCurrency);
    bool bDstXrp = isXRP(mDstAmount.getCurrency());

    if (!mLedger->exists(keylet::account(mSrcAccount)))
    {
        // We can't even start without a source account.
        JLOG(j_.debug()) << "invalid source account";
        return false;
    }

    if ((mEffectiveDst != mDstAccount) &&
        !mLedger->exists(keylet::account(mEffectiveDst)))
    {
        JLOG(j_.debug()) << "Non-existent gateway";
        return false;
    }

    if (!mLedger->exists(keylet::account(mDstAccount)))
    {
        // Can't find the destination account - we must be funding a new
        // account.
        if (!bDstXrp)
        {
            JLOG(j_.debug()) << "New account not being funded in XRP ";
            return false;
        }

        auto const reserve = STAmount(mLedger->fees().accountReserve(0));
        if (mDstAmount < reserve)
        {
            JLOG(j_.debug())
                << "New account not getting enough funding: " << mDstAmount
                << " < " << reserve;
            return false;
        }
    }

    // Now compute the payment type from the types of the source and destination
    // currencies.
    PaymentType paymentType;
    if (bSrcXrp && bDstXrp)
    {
        // XRP -> XRP
        JLOG(j_.debug()) << "XRP to XRP payment";
        paymentType = pt_XRP_to_XRP;
    }
    else if (bSrcXrp)
    {
        // XRP -> non-XRP
        JLOG(j_.debug()) << "XRP to non-XRP payment";
        paymentType = pt_XRP_to_nonXRP;
    }
    else if (bDstXrp)
    {
        // non-XRP -> XRP
        JLOG(j_.debug()) << "non-XRP to XRP payment";
        paymentType = pt_nonXRP_to_XRP;
    }
    else if (mSrcCurrency == mDstAmount.getCurrency())
    {
        // non-XRP -> non-XRP - Same currency
        JLOG(j_.debug()) << "non-XRP to non-XRP - same currency";
        paymentType = pt_nonXRP_to_same;
    }
    else
    {
        // non-XRP to non-XRP - Different currency
        JLOG(j_.debug()) << "non-XRP to non-XRP - cross currency";
        paymentType = pt_nonXRP_to_nonXRP;
    }

    // Now iterate over all paths for that paymentType.
    for (auto const& costedPath : mPathTable[paymentType])
    {
        // Only use paths with at most the current search level.
        if (costedPath.searchLevel <= searchLevel)
        {
            addPathsForType(costedPath.type);

            // TODO(tom): we might be missing other good paths with this
            // arbitrary cut off.
            if (mCompletePaths.size() > PATHFINDER_MAX_COMPLETE_PATHS)
                break;
        }
    }

    JLOG(j_.debug()) << mCompletePaths.size() << " complete paths found";

    // Even if we find no paths, default paths may work, and we don't check them
    // currently.
    return true;
}

TER
Pathfinder::getPathLiquidity(
    STPath const& path,            // IN:  The path to check.
    STAmount const& minDstAmount,  // IN:  The minimum output this path must
                                   //      deliver to be worth keeping.
    STAmount& amountOut,           // OUT: The actual liquidity along the path.
    uint64_t& qualityOut) const    // OUT: The returned initial quality
{
    STPathSet pathSet;
    pathSet.push_back(path);

    path::RippleCalc::Input rcInput;
    rcInput.defaultPathsAllowed = false;

    PaymentSandbox sandbox(&*mLedger, tapNONE);

    try
    {
        // Compute a path that provides at least the minimum liquidity.
        if (convert_all_)
            rcInput.partialPaymentAllowed = true;

        auto rc = path::RippleCalc::rippleCalculate(
            sandbox,
            mSrcAmount,
            minDstAmount,
            mDstAccount,
            mSrcAccount,
            pathSet,
            app_.logs(),
            &rcInput);
        // If we can't get even the minimum liquidity requested, we're done.
        if (rc.result() != tesSUCCESS)
            return rc.result();

        qualityOut = getRate(rc.actualAmountOut, rc.actualAmountIn);
        amountOut = rc.actualAmountOut;

        if (!convert_all_)
        {
            // Now try to compute the remaining liquidity.
            rcInput.partialPaymentAllowed = true;
            rc = path::RippleCalc::rippleCalculate(
                sandbox,
                mSrcAmount,
                mDstAmount - amountOut,
                mDstAccount,
                mSrcAccount,
                pathSet,
                app_.logs(),
                &rcInput);

            // If we found further liquidity, add it into the result.
            if (rc.result() == tesSUCCESS)
                amountOut += rc.actualAmountOut;
        }

        return tesSUCCESS;
    }
    catch (std::exception const& e)
    {
        JLOG(j_.info()) << "checkpath: exception (" << e.what() << ") "
                        << path.getJson(JsonOptions::none);
        return tefEXCEPTION;
    }
}

namespace {

// Return the smallest amount of useful liquidity for a given amount, and the
// total number of paths we have to evaluate.
STAmount
smallestUsefulAmount(STAmount const& amount, int maxPaths)
{
    return divide(amount, STAmount(maxPaths + 2), amount.issue());
}

}  // namespace

void
Pathfinder::computePathRanks(int maxPaths)
{
    mRemainingAmount = convert_all_
        ? STAmount(
              mDstAmount.issue(), STAmount::cMaxValue, STAmount::cMaxOffset)
        : mDstAmount;

    // Must subtract liquidity in default path from remaining amount.
    try
    {
        PaymentSandbox sandbox(&*mLedger, tapNONE);

        path::RippleCalc::Input rcInput;
        rcInput.partialPaymentAllowed = true;
        auto rc = path::RippleCalc::rippleCalculate(
            sandbox,
            mSrcAmount,
            mRemainingAmount,
            mDstAccount,
            mSrcAccount,
            STPathSet(),
            app_.logs(),
            &rcInput);

        if (rc.result() == tesSUCCESS)
        {
            JLOG(j_.debug())
                << "Default path contributes: " << rc.actualAmountIn;
            mRemainingAmount -= rc.actualAmountOut;
        }
        else
        {
            JLOG(j_.debug())
                << "Default path fails: " << transToken(rc.result());
        }
    }
    catch (std::exception const&)
    {
        JLOG(j_.debug()) << "Default path causes exception";
    }

    rankPaths(maxPaths, mCompletePaths, mPathRanks);
}

static bool
isDefaultPath(STPath const& path)
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

static STPath
removeIssuer(STPath const& path)
{
    // This path starts with the issuer, which is already implied
    // so remove the head node
    STPath ret;

    for (auto it = path.begin() + 1; it != path.end(); ++it)
        ret.push_back(*it);

    return ret;
}

// For each useful path in the input path set,
// create a ranking entry in the output vector of path ranks
void
Pathfinder::rankPaths(
    int maxPaths,
    STPathSet const& paths,
    std::vector<PathRank>& rankedPaths)
{
    rankedPaths.clear();
    rankedPaths.reserve(paths.size());

    STAmount saMinDstAmount;
    if (convert_all_)
    {
        // On convert_all_ partialPaymentAllowed will be set to true
        // and requiring a huge amount will find the highest liquidity.
        saMinDstAmount = STAmount(
            mDstAmount.issue(), STAmount::cMaxValue, STAmount::cMaxOffset);
    }
    else
    {
        // Ignore paths that move only very small amounts.
        saMinDstAmount = smallestUsefulAmount(mDstAmount, maxPaths);
    }

    for (int i = 0; i < paths.size(); ++i)
    {
        auto const& currentPath = paths[i];
        if (!currentPath.empty())
        {
            STAmount liquidity;
            uint64_t uQuality;
            auto const resultCode = getPathLiquidity(
                currentPath, saMinDstAmount, liquidity, uQuality);
            if (resultCode != tesSUCCESS)
            {
                JLOG(j_.debug())
                    << "findPaths: dropping : " << transToken(resultCode)
                    << ": " << currentPath.getJson(JsonOptions::none);
            }
            else
            {
                JLOG(j_.debug()) << "findPaths: quality: " << uQuality << ": "
                                 << currentPath.getJson(JsonOptions::none);

                rankedPaths.push_back(
                    {uQuality, currentPath.size(), liquidity, i});
            }
        }
    }

    // Sort paths by:
    //    cost of path (when considering quality)
    //    width of path
    //    length of path
    // A better PathRank is lower, best are sorted to the beginning.
    std::sort(
        rankedPaths.begin(),
        rankedPaths.end(),
        [&](Pathfinder::PathRank const& a, Pathfinder::PathRank const& b) {
            // 1) Higher quality (lower cost) is better
            if (!convert_all_ && a.quality != b.quality)
                return a.quality < b.quality;

            // 2) More liquidity (higher volume) is better
            if (a.liquidity != b.liquidity)
                return a.liquidity > b.liquidity;

            // 3) Shorter paths are better
            if (a.length != b.length)
                return a.length < b.length;

            // 4) Tie breaker
            return a.index > b.index;
        });
}

STPathSet
Pathfinder::getBestPaths(
    int maxPaths,
    STPath& fullLiquidityPath,
    STPathSet const& extraPaths,
    AccountID const& srcIssuer)
{
    JLOG(j_.debug()) << "findPaths: " << mCompletePaths.size() << " paths and "
                     << extraPaths.size() << " extras";

    if (mCompletePaths.empty() && extraPaths.empty())
        return mCompletePaths;

    assert(fullLiquidityPath.empty());
    const bool issuerIsSender =
        isXRP(mSrcCurrency) || (srcIssuer == mSrcAccount);

    std::vector<PathRank> extraPathRanks;
    rankPaths(maxPaths, extraPaths, extraPathRanks);

    STPathSet bestPaths;

    // The best PathRanks are now at the start.  Pull off enough of them to
    // fill bestPaths, then look through the rest for the best individual
    // path that can satisfy the entire liquidity - if one exists.
    STAmount remaining = mRemainingAmount;

    auto pathsIterator = mPathRanks.begin();
    auto extraPathsIterator = extraPathRanks.begin();

    while (pathsIterator != mPathRanks.end() ||
           extraPathsIterator != extraPathRanks.end())
    {
        bool usePath = false;
        bool useExtraPath = false;

        if (pathsIterator == mPathRanks.end())
            useExtraPath = true;
        else if (extraPathsIterator == extraPathRanks.end())
            usePath = true;
        else if (extraPathsIterator->quality < pathsIterator->quality)
            useExtraPath = true;
        else if (extraPathsIterator->quality > pathsIterator->quality)
            usePath = true;
        else if (extraPathsIterator->liquidity > pathsIterator->liquidity)
            useExtraPath = true;
        else if (extraPathsIterator->liquidity < pathsIterator->liquidity)
            usePath = true;
        else
        {
            // Risk is high they have identical liquidity
            useExtraPath = true;
            usePath = true;
        }

        auto& pathRank = usePath ? *pathsIterator : *extraPathsIterator;

        auto const& path = usePath ? mCompletePaths[pathRank.index]
                                   : extraPaths[pathRank.index];

        if (useExtraPath)
            ++extraPathsIterator;

        if (usePath)
            ++pathsIterator;

        auto iPathsLeft = maxPaths - bestPaths.size();
        if (!(iPathsLeft > 0 || fullLiquidityPath.empty()))
            break;

        if (path.empty())
        {
            assert(false);
            continue;
        }

        bool startsWithIssuer = false;

        if (!issuerIsSender && usePath)
        {
            // Need to make sure path matches issuer constraints
            if (isDefaultPath(path) || path.front().getAccountID() != srcIssuer)
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
            bestPaths.push_back(startsWithIssuer ? removeIssuer(path) : path);
        }
        else if (
            iPathsLeft == 0 && pathRank.liquidity >= mDstAmount &&
            fullLiquidityPath.empty())
        {
            // We found an extra path that can move the whole amount.
            fullLiquidityPath = (startsWithIssuer ? removeIssuer(path) : path);
            JLOG(j_.debug()) << "Found extra full path: "
                             << fullLiquidityPath.getJson(JsonOptions::none);
        }
        else
        {
            JLOG(j_.debug()) << "Skipping a non-filling path: "
                             << path.getJson(JsonOptions::none);
        }
    }

    if (remaining > beast::zero)
    {
        assert(fullLiquidityPath.empty());
        JLOG(j_.info()) << "Paths could not send " << remaining << " of "
                        << mDstAmount;
    }
    else
    {
        JLOG(j_.debug()) << "findPaths: RESULTS: "
                         << bestPaths.getJson(JsonOptions::none);
    }
    return bestPaths;
}

bool
Pathfinder::issueMatchesOrigin(Issue const& issue)
{
    bool matchingCurrency = (issue.currency == mSrcCurrency);
    bool matchingAccount = isXRP(issue.currency) ||
        (mSrcIssuer && issue.account == mSrcIssuer) ||
        issue.account == mSrcAccount;

    return matchingCurrency && matchingAccount;
}

int
Pathfinder::getPathsOut(
    Currency const& currency,
    AccountID const& account,
    bool isDstCurrency,
    AccountID const& dstAccount)
{
    Issue const issue(currency, account);

    auto [it, inserted] = mPathsOutCountMap.emplace(issue, 0);

    // If it was already present, return the stored number of paths
    if (!inserted)
        return it->second;

    auto sleAccount = mLedger->read(keylet::account(account));

    if (!sleAccount)
        return 0;

    int aFlags = sleAccount->getFieldU32(sfFlags);
    bool const bAuthRequired = (aFlags & lsfRequireAuth) != 0;
    bool const bFrozen = ((aFlags & lsfGlobalFreeze) != 0);

    int count = 0;

    if (!bFrozen)
    {
        count = app_.getOrderBookDB().getBookSize(issue);

        for (auto const& item : mRLCache->getRippleLines(account))
        {
            RippleState* rspEntry = (RippleState*)item.get();

            if (currency != rspEntry->getLimit().getCurrency())
            {
            }
            else if (
                rspEntry->getBalance() <= beast::zero &&
                (!rspEntry->getLimitPeer() ||
                 -rspEntry->getBalance() >= rspEntry->getLimitPeer() ||
                 (bAuthRequired && !rspEntry->getAuth())))
            {
            }
            else if (
                isDstCurrency && dstAccount == rspEntry->getAccountIDPeer())
            {
                count += 10000;  // count a path to the destination extra
            }
            else if (rspEntry->getNoRipplePeer())
            {
                // This probably isn't a useful path out
            }
            else if (rspEntry->getFreezePeer())
            {
                // Not a useful path out
            }
            else
            {
                ++count;
            }
        }
    }
    it->second = count;
    return count;
}

void
Pathfinder::addLinks(
    STPathSet const& currentPaths,  // The paths to build from
    STPathSet& incompletePaths,     // The set of partial paths we add to
    int addFlags)
{
    JLOG(j_.debug()) << "addLink< on " << currentPaths.size()
                     << " source(s), flags=" << addFlags;
    for (auto const& path : currentPaths)
        addLink(path, incompletePaths, addFlags);
}

STPathSet&
Pathfinder::addPathsForType(PathType const& pathType)
{
    // See if the set of paths for this type already exists.
    auto it = mPaths.find(pathType);
    if (it != mPaths.end())
        return it->second;

    // Otherwise, if the type has no nodes, return the empty path.
    if (pathType.empty())
        return mPaths[pathType];

    // Otherwise, get the paths for the parent PathType by calling
    // addPathsForType recursively.
    PathType parentPathType = pathType;
    parentPathType.pop_back();

    STPathSet const& parentPaths = addPathsForType(parentPathType);
    STPathSet& pathsOut = mPaths[pathType];

    JLOG(j_.debug()) << "getPaths< adding onto '"
                     << pathTypeToString(parentPathType) << "' to get '"
                     << pathTypeToString(pathType) << "'";

    int initialSize = mCompletePaths.size();

    // Add the last NodeType to the lists.
    auto nodeType = pathType.back();
    switch (nodeType)
    {
        case nt_SOURCE:
            // Source must always be at the start, so pathsOut has to be empty.
            assert(pathsOut.empty());
            pathsOut.push_back(STPath());
            break;

        case nt_ACCOUNTS:
            addLinks(parentPaths, pathsOut, afADD_ACCOUNTS);
            break;

        case nt_BOOKS:
            addLinks(parentPaths, pathsOut, afADD_BOOKS);
            break;

        case nt_XRP_BOOK:
            addLinks(parentPaths, pathsOut, afADD_BOOKS | afOB_XRP);
            break;

        case nt_DEST_BOOK:
            addLinks(parentPaths, pathsOut, afADD_BOOKS | afOB_LAST);
            break;

        case nt_DESTINATION:
            // FIXME: What if a different issuer was specified on the
            // destination amount?
            // TODO(tom): what does this even mean?  Should it be a JIRA?
            addLinks(parentPaths, pathsOut, afADD_ACCOUNTS | afAC_LAST);
            break;
    }

    if (mCompletePaths.size() != initialSize)
    {
        JLOG(j_.debug()) << (mCompletePaths.size() - initialSize)
                         << " complete paths added";
    }

    JLOG(j_.debug()) << "getPaths> " << pathsOut.size()
                     << " partial paths found";
    return pathsOut;
}

bool
Pathfinder::isNoRipple(
    AccountID const& fromAccount,
    AccountID const& toAccount,
    Currency const& currency)
{
    auto sleRipple =
        mLedger->read(keylet::line(toAccount, fromAccount, currency));

    auto const flag(
        (toAccount > fromAccount) ? lsfHighNoRipple : lsfLowNoRipple);

    return sleRipple && (sleRipple->getFieldU32(sfFlags) & flag);
}

// Does this path end on an account-to-account link whose last account has
// set "no ripple" on the link?
bool
Pathfinder::isNoRippleOut(STPath const& currentPath)
{
    // Must have at least one link.
    if (currentPath.empty())
        return false;

    // Last link must be an account.
    STPathElement const& endElement = currentPath.back();
    if (!(endElement.getNodeType() & STPathElement::typeAccount))
        return false;

    // If there's only one item in the path, return true if that item specifies
    // no ripple on the output. A path with no ripple on its output can't be
    // followed by a link with no ripple on its input.
    auto const& fromAccount = (currentPath.size() == 1)
        ? mSrcAccount
        : (currentPath.end() - 2)->getAccountID();
    auto const& toAccount = endElement.getAccountID();
    return isNoRipple(fromAccount, toAccount, endElement.getCurrency());
}

void
addUniquePath(STPathSet& pathSet, STPath const& path)
{
    // TODO(tom): building an STPathSet this way is quadratic in the size
    // of the STPathSet!
    for (auto const& p : pathSet)
    {
        if (p == path)
            return;
    }
    pathSet.push_back(path);
}

void
Pathfinder::addLink(
    const STPath& currentPath,   // The path to build from
    STPathSet& incompletePaths,  // The set of partial paths we add to
    int addFlags)
{
    auto const& pathEnd = currentPath.empty() ? mSource : currentPath.back();
    auto const& uEndCurrency = pathEnd.getCurrency();
    auto const& uEndIssuer = pathEnd.getIssuerID();
    auto const& uEndAccount = pathEnd.getAccountID();
    bool const bOnXRP = uEndCurrency.isZero();

    // Does pathfinding really need to get this to
    // a gateway (the issuer of the destination amount)
    // rather than the ultimate destination?
    bool const hasEffectiveDestination = mEffectiveDst != mDstAccount;

    JLOG(j_.trace()) << "addLink< flags=" << addFlags << " onXRP=" << bOnXRP;
    JLOG(j_.trace()) << currentPath.getJson(JsonOptions::none);

    if (addFlags & afADD_ACCOUNTS)
    {
        // add accounts
        if (bOnXRP)
        {
            if (mDstAmount.native() && !currentPath.empty())
            {  // non-default path to XRP destination
                JLOG(j_.trace()) << "complete path found ax: "
                                 << currentPath.getJson(JsonOptions::none);
                addUniquePath(mCompletePaths, currentPath);
            }
        }
        else
        {
            // search for accounts to add
            auto const sleEnd = mLedger->read(keylet::account(uEndAccount));

            if (sleEnd)
            {
                bool const bRequireAuth(
                    sleEnd->getFieldU32(sfFlags) & lsfRequireAuth);
                bool const bIsEndCurrency(
                    uEndCurrency == mDstAmount.getCurrency());
                bool const bIsNoRippleOut(isNoRippleOut(currentPath));
                bool const bDestOnly(addFlags & afAC_LAST);

                auto& rippleLines(mRLCache->getRippleLines(uEndAccount));

                AccountCandidates candidates;
                candidates.reserve(rippleLines.size());

                for (auto const& item : rippleLines)
                {
                    auto* rs = dynamic_cast<RippleState const*>(item.get());
                    if (!rs)
                    {
                        JLOG(j_.error()) << "Couldn't decipher RippleState";
                        continue;
                    }
                    auto const& acct = rs->getAccountIDPeer();

                    if (hasEffectiveDestination && (acct == mDstAccount))
                    {
                        // We skipped the gateway
                        continue;
                    }

                    bool bToDestination = acct == mEffectiveDst;

                    if (bDestOnly && !bToDestination)
                    {
                        continue;
                    }

                    if ((uEndCurrency == rs->getLimit().getCurrency()) &&
                        !currentPath.hasSeen(acct, uEndCurrency, acct))
                    {
                        // path is for correct currency and has not been seen
                        if (rs->getBalance() <= beast::zero &&
                            (!rs->getLimitPeer() ||
                             -rs->getBalance() >= rs->getLimitPeer() ||
                             (bRequireAuth && !rs->getAuth())))
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
                                    JLOG(j_.trace())
                                        << "complete path found ae: "
                                        << currentPath.getJson(
                                               JsonOptions::none);
                                    addUniquePath(mCompletePaths, currentPath);
                                }
                            }
                            else if (!bDestOnly)
                            {
                                // this is a high-priority candidate
                                candidates.push_back(
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
                            int out = getPathsOut(
                                uEndCurrency,
                                acct,
                                bIsEndCurrency,
                                mEffectiveDst);
                            if (out)
                                candidates.push_back({out, acct});
                        }
                    }
                }

                if (!candidates.empty())
                {
                    std::sort(
                        candidates.begin(),
                        candidates.end(),
                        std::bind(
                            compareAccountCandidate,
                            mLedger->seq(),
                            std::placeholders::_1,
                            std::placeholders::_2));

                    int count = candidates.size();
                    // allow more paths from source
                    if ((count > 10) && (uEndAccount != mSrcAccount))
                        count = 10;
                    else if (count > 50)
                        count = 50;

                    auto it = candidates.begin();
                    while (count-- != 0)
                    {
                        // Add accounts to incompletePaths
                        STPathElement pathElement(
                            STPathElement::typeAccount,
                            it->account,
                            uEndCurrency,
                            it->account);
                        incompletePaths.assembleAdd(currentPath, pathElement);
                        ++it;
                    }
                }
            }
            else
            {
                JLOG(j_.warn()) << "Path ends on non-existent issuer";
            }
        }
    }
    if (addFlags & afADD_BOOKS)
    {
        // add order books
        if (addFlags & afOB_XRP)
        {
            // to XRP only
            if (!bOnXRP &&
                app_.getOrderBookDB().isBookToXRP({uEndCurrency, uEndIssuer}))
            {
                STPathElement pathElement(
                    STPathElement::typeCurrency,
                    xrpAccount(),
                    xrpCurrency(),
                    xrpAccount());
                incompletePaths.assembleAdd(currentPath, pathElement);
            }
        }
        else
        {
            bool bDestOnly = (addFlags & afOB_LAST) != 0;
            auto books = app_.getOrderBookDB().getBooksByTakerPays(
                {uEndCurrency, uEndIssuer});
            JLOG(j_.trace())
                << books.size() << " books found from this currency/issuer";

            for (auto const& book : books)
            {
                if (!currentPath.hasSeen(
                        xrpAccount(),
                        book->getCurrencyOut(),
                        book->getIssuerOut()) &&
                    !issueMatchesOrigin(book->book().out) &&
                    (!bDestOnly ||
                     (book->getCurrencyOut() == mDstAmount.getCurrency())))
                {
                    STPath newPath(currentPath);

                    if (book->getCurrencyOut().isZero())
                    {  // to XRP

                        // add the order book itself
                        newPath.emplace_back(
                            STPathElement::typeCurrency,
                            xrpAccount(),
                            xrpCurrency(),
                            xrpAccount());

                        if (mDstAmount.getCurrency().isZero())
                        {
                            // destination is XRP, add account and path is
                            // complete
                            JLOG(j_.trace())
                                << "complete path found bx: "
                                << currentPath.getJson(JsonOptions::none);
                            addUniquePath(mCompletePaths, newPath);
                        }
                        else
                            incompletePaths.push_back(newPath);
                    }
                    else if (!currentPath.hasSeen(
                                 book->getIssuerOut(),
                                 book->getCurrencyOut(),
                                 book->getIssuerOut()))
                    {
                        // Don't want the book if we've already seen the issuer
                        // book -> account -> book
                        if ((newPath.size() >= 2) &&
                            (newPath.back().isAccount()) &&
                            (newPath[newPath.size() - 2].isOffer()))
                        {
                            // replace the redundant account with the order book
                            newPath[newPath.size() - 1] = STPathElement(
                                STPathElement::typeCurrency |
                                    STPathElement::typeIssuer,
                                xrpAccount(),
                                book->getCurrencyOut(),
                                book->getIssuerOut());
                        }
                        else
                        {
                            // add the order book
                            newPath.emplace_back(
                                STPathElement::typeCurrency |
                                    STPathElement::typeIssuer,
                                xrpAccount(),
                                book->getCurrencyOut(),
                                book->getIssuerOut());
                        }

                        if (hasEffectiveDestination &&
                            book->getIssuerOut() == mDstAccount &&
                            book->getCurrencyOut() == mDstAmount.getCurrency())
                        {
                            // We skipped a required issuer
                        }
                        else if (
                            book->getIssuerOut() == mEffectiveDst &&
                            book->getCurrencyOut() == mDstAmount.getCurrency())
                        {  // with the destination account, this path is
                           // complete
                            JLOG(j_.trace())
                                << "complete path found ba: "
                                << currentPath.getJson(JsonOptions::none);
                            addUniquePath(mCompletePaths, newPath);
                        }
                        else
                        {
                            // add issuer's account, path still incomplete
                            incompletePaths.assembleAdd(
                                newPath,
                                STPathElement(
                                    STPathElement::typeAccount,
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

namespace {

Pathfinder::PathType
makePath(char const* string)
{
    Pathfinder::PathType ret;

    while (true)
    {
        switch (*string++)
        {
            case 's':  // source
                ret.push_back(Pathfinder::nt_SOURCE);
                break;

            case 'a':  // accounts
                ret.push_back(Pathfinder::nt_ACCOUNTS);
                break;

            case 'b':  // books
                ret.push_back(Pathfinder::nt_BOOKS);
                break;

            case 'x':  // xrp book
                ret.push_back(Pathfinder::nt_XRP_BOOK);
                break;

            case 'f':  // book to final currency
                ret.push_back(Pathfinder::nt_DEST_BOOK);
                break;

            case 'd':
                // Destination (with account, if required and not already
                // present).
                ret.push_back(Pathfinder::nt_DESTINATION);
                break;

            case 0:
                return ret;
        }
    }
}

void
fillPaths(Pathfinder::PaymentType type, PathCostList const& costs)
{
    auto& list = mPathTable[type];
    assert(list.empty());
    for (auto& cost : costs)
        list.push_back({cost.cost, makePath(cost.path)});
}

}  // namespace

// Costs:
// 0 = minimum to make some payments possible
// 1 = include trivial paths to make common cases work
// 4 = normal fast search level
// 7 = normal slow search level
// 10 = most agressive

void
Pathfinder::initPathTable()
{
    // CAUTION: Do not include rules that build default paths

    mPathTable.clear();
    fillPaths(pt_XRP_to_XRP, {});

    fillPaths(
        pt_XRP_to_nonXRP,
        {{1, "sfd"},    // source -> book -> gateway
         {3, "sfad"},   // source -> book -> account -> destination
         {5, "sfaad"},  // source -> book -> account -> account -> destination
         {6, "sbfd"},   // source -> book -> book -> destination
         {8, "sbafd"},  // source -> book -> account -> book -> destination
         {9, "sbfad"},  // source -> book -> book -> account -> destination
         {10, "sbafad"}});

    fillPaths(
        pt_nonXRP_to_XRP,
        {{1, "sxd"},   // gateway buys XRP
         {2, "saxd"},  // source -> gateway -> book(XRP) -> dest
         {6, "saaxd"},
         {7, "sbxd"},
         {8, "sabxd"},
         {9, "sabaxd"}});

    // non-XRP to non-XRP (same currency)
    fillPaths(
        pt_nonXRP_to_same,
        {
            {1, "sad"},   // source -> gateway -> destination
            {1, "sfd"},   // source -> book -> destination
            {4, "safd"},  // source -> gateway -> book -> destination
            {4, "sfad"},
            {5, "saad"},
            {5, "sbfd"},
            {6, "sxfad"},
            {6, "safad"},
            {6, "saxfd"},  // source -> gateway -> book to XRP -> book ->
                           // destination
            {6, "saxfad"},
            {6, "sabfd"},  // source -> gateway -> book -> book -> destination
            {7, "saaad"},
        });

    // non-XRP to non-XRP (different currency)
    fillPaths(
        pt_nonXRP_to_nonXRP,
        {
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

}  // namespace ripple
