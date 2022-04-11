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

#ifndef RIPPLE_LEDGER_RULES_H_INCLUDED
#define RIPPLE_LEDGER_RULES_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/beast/hash/uhash.h>
#include <ripple/protocol/STVector256.h>
#include <unordered_set>

namespace ripple {

class DigestAwareReadView;

/** Rules controlling protocol behavior. */
class Rules
{
private:
    class Impl;

    // Carrying impl by shared_ptr makes Rules comparatively cheap to pass
    // by value.
    std::shared_ptr<Impl const> impl_;

public:
    Rules(Rules const&) = default;

    Rules&
    operator=(Rules const&) = default;

    Rules() = delete;

    /** Construct an empty rule set.

        These are the rules reflected by
        the genesis ledger.
    */
    explicit Rules(std::unordered_set<uint256, beast::uhash<>> const& presets);

private:
    // Allow a friend function to construct Rules.
    friend Rules
    makeRulesGivenLedger(
        DigestAwareReadView const& ledger,
        std::unordered_set<uint256, beast::uhash<>> const& presets);

    Rules(
        std::unordered_set<uint256, beast::uhash<>> const& presets,
        std::optional<uint256> const& digest,
        STVector256 const& amendments);

public:
    /** Returns `true` if a feature is enabled. */
    bool
    enabled(uint256 const& feature) const;

    /** Returns `true` if two rule sets are identical.

        @note This is for diagnostics.
    */
    bool
    operator==(Rules const&) const;

    bool
    operator!=(Rules const& other) const;
};

}  // namespace ripple
#endif
