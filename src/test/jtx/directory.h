//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_JTX_DIRECTORY_H_INCLUDED
#define RIPPLE_TEST_JTX_DIRECTORY_H_INCLUDED

#include <test/jtx/Env.h>

#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>

#include <cstdint>
#include <limits>

namespace ripple::test::jtx {

/** Directory operations. */
namespace directory {

enum Error {
    DirectoryRootNotFound,
    DirectoryTooSmall,
    DirectoryPageDuplicate,
    DirectoryPageNotFound,
    InvalidLastPage,
    AdjustmentError
};

/// Move the position of the last page in the user's directory on open ledger to
/// newLastPage. Requirements:
/// - directory must have at least two pages (root and one more)
/// - adjust should be used to update owner nodes of the objects affected
/// - newLastPage must be greater than index of the last page in the directory
///
/// Use this to test tecDIR_FULL errors in open ledger.
/// NOTE: effects will be DISCARDED on env.close()
auto
bumpLastPage(
    Env& env,
    std::uint64_t newLastPage,
    Keylet directory,
    std::function<bool(ApplyView&, uint256, std::uint64_t)> adjust)
    -> Expected<void, Error>;

/// Implementation of adjust for the most common ledger entry, i.e. one where
/// page index is stored in sfOwnerNode (and only there). Pass this function
/// to bumpLastPage if the last page of directory has only objects
/// of this kind (e.g. ticket, DID, offer, deposit preauth, MPToken etc.)
bool
adjustOwnerNode(ApplyView& view, uint256 key, std::uint64_t page);

inline auto
maximumPageIndex(Env const& env) -> std::uint64_t
{
    if (env.enabled(fixDirectoryLimit))
        return std::numeric_limits<std::uint64_t>::max();
    return dirNodeMaxPages - 1;
}

}  // namespace directory

}  // namespace ripple::test::jtx

#endif
