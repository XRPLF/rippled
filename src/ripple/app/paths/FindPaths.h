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

#ifndef RIPPLE_APP_PATHS_FINDPATHS_H_INCLUDED
#define RIPPLE_APP_PATHS_FINDPATHS_H_INCLUDED

#include <ripple/app/paths/RippleLineCache.h>

namespace ripple {

class FindPaths
{
public:
    FindPaths (
        RippleLineCache::ref cache,
        AccountID const& srcAccount,
        AccountID const& dstAccount,
        STAmount const& dstAmount,
        boost::optional<STAmount> const& srcAmount,
        /** searchLevel is the maximum search level allowed in an output path.
         */
        int searchLevel,
        /** maxPaths is the maximum number of paths that can be returned in
            pathsOut. */
        unsigned int const maxPaths);
    ~FindPaths();

    /** The return value will have any additional paths found. Only
        non-default paths without source or destination will be added. */
    boost::optional<STPathSet>
    findPathsForIssue (
        Issue const& issue,

        /** Contains any paths you want to ensure are
            included if still good. */
        STPathSet const& paths,

        /** On input, fullLiquidityPath must be an empty STPath.

            On output, if fullLiquidityPath is non-empty, it contains one extra
            path that can move the entire liquidity requested. */
        STPath& fullLiquidityPath,

        Application& app);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

/** The return value will have any additional paths found. Only
    non-default paths without source or destination will be added. */
boost::optional<STPathSet>
findPathsForOneIssuer (
    RippleLineCache::ref cache,
    AccountID const& srcAccount,
    AccountID const& dstAccount,
    Issue const& srcIssue,
    STAmount const& dstAmount,

    /** searchLevel is the maximum search level allowed in an output path. */
    int searchLevel,

    /** maxPaths is the maximum number of paths that can be returned in
        pathsOut. */
    unsigned int const maxPaths,

    /** Contains any paths you want to ensure are included if
        still good.
    */
    STPathSet const& paths,

    /** On input, fullLiquidityPath must be an empty STPath.

        On output, if fullLiquidityPath is non-empty, it contains one extra path
        that can move the entire liquidity requested. */
    STPath& fullLiquidityPath,

    Application& app);

void initializePathfinding ();

} // ripple

#endif
