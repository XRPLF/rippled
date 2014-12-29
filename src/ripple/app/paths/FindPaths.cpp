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

#include <BeastConfig.h>
#include <ripple/app/paths/FindPaths.h>
#include <ripple/app/paths/Pathfinder.h>

namespace ripple {

class FindPaths::Impl {
public:
    Impl (
        RippleLineCache::ref cache,
        Account const& srcAccount,
        Account const& dstAccount,
        STAmount const& dstAmount,
        int searchLevel,
        unsigned int maxPaths)
            : cache_ (cache),
              srcAccount_ (srcAccount),
              dstAccount_ (dstAccount),
              dstAmount_ (dstAmount),
              searchLevel_ (searchLevel),
              maxPaths_ (maxPaths)
    {
    }


    bool findPathsForIssue (
        Issue const& issue,
        STPathSet& pathsInOut,
        STPath& fullLiquidityPath)
    {
        if (auto& pathfinder = getPathFinder (issue.currency))
        {
            pathsInOut = pathfinder->getBestPaths (
                maxPaths_,  fullLiquidityPath, pathsInOut, issue.account);
            return true;
        }
        assert (false);
        return false;
    }

private:
    hash_map<Currency, std::unique_ptr<Pathfinder>> currencyMap_;

    RippleLineCache::ref cache_;
    Account const srcAccount_;
    Account const dstAccount_;
    STAmount const dstAmount_;
    int const searchLevel_;
    unsigned int const maxPaths_;

    std::unique_ptr<Pathfinder> const& getPathFinder (Currency const& currency)
    {
        auto i = currencyMap_.find (currency);
        if (i != currencyMap_.end ())
            return i->second;
        auto pathfinder = std::make_unique<Pathfinder> (
            cache_, srcAccount_, dstAccount_, currency, dstAmount_);
        if (pathfinder->findPaths (searchLevel_))
            pathfinder->computePathRanks (maxPaths_);
        else
            pathfinder.reset ();  // It's a bad request - clear it.
        return currencyMap_[currency] = std::move (pathfinder);

        // TODO(tom): why doesn't this faster way compile?
        // return currencyMap_.insert (i, std::move (pathfinder)).second;
    }
};

FindPaths::FindPaths (
    RippleLineCache::ref cache,
    Account const& srcAccount,
    Account const& dstAccount,
    STAmount const& dstAmount,
    int level,
    unsigned int maxPaths)
        : impl_ (std::make_unique<Impl> (
              cache, srcAccount, dstAccount, dstAmount, level, maxPaths))
{
}

FindPaths::~FindPaths() = default;

bool FindPaths::findPathsForIssue (
    Issue const& issue,
    STPathSet& pathsInOut,
    STPath& fullLiquidityPath)
{
    return impl_->findPathsForIssue (issue, pathsInOut, fullLiquidityPath);
}

bool findPathsForOneIssuer (
    RippleLineCache::ref cache,
    Account const& srcAccount,
    Account const& dstAccount,
    Issue const& srcIssue,
    STAmount const& dstAmount,
    int searchLevel,
    unsigned int const maxPaths,
    STPathSet& pathsInOut,
    STPath& fullLiquidityPath)
{
    Pathfinder pf (
        cache,
        srcAccount,
        dstAccount,
        srcIssue.currency,
        srcIssue.account,
        dstAmount);

    if (!pf.findPaths (searchLevel))
        return false;

    pf.computePathRanks (maxPaths);
    pathsInOut = pf.getBestPaths(maxPaths, fullLiquidityPath, pathsInOut, srcIssue.account);
    return true;
}

void initializePathfinding ()
{
    Pathfinder::initPathTable ();
}

} // ripple
