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
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/app/tx/impl/InvariantCheck.h>
#include <ripple/app/tx/impl/Transactor.h>
#include <ripple/basics/Log.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Feature.h>
#include <cassert>

namespace ripple {

ApplyContext::ApplyContext(Application& app_,
    OpenView& base, STTx const& tx_, TER preclaimResult_,
        std::uint64_t baseFee_, ApplyFlags flags,
            beast::Journal journal_)
    : app(app_)
    , tx(tx_)
    , preclaimResult(preclaimResult_)
    , baseFee(baseFee_)
    , journal(journal_)
    , base_ (base)
    , flags_(flags)
{
    view_.emplace(&base_, flags_);
}

void
ApplyContext::discard()
{
    view_.emplace(&base_, flags_);
}

void
ApplyContext::apply(TER ter)
{
    view_->apply(base_, tx, ter, journal);
}

std::size_t
ApplyContext::size()
{
    return view_->size();
}

void
ApplyContext::visit (std::function <void (
    uint256 const&, bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)> const& func)
{
    view_->visit(base_, func);
}

template<std::size_t... Is>
TER
ApplyContext::checkInvariantsHelper(TER terResult, std::index_sequence<Is...>)
{
    if (view_->rules().enabled(featureEnforceInvariants))
    {
        auto checkers = getInvariantChecks();

        // call each check's per-entry method
        visit (
            [&checkers](
                uint256 const& index,
                bool isDelete,
                std::shared_ptr <SLE const> const& before,
                std::shared_ptr <SLE const> const& after)
            {
                // Sean Parent for_each_argument trick
                (void)std::array<int, sizeof...(Is)>{
                    {((std::get<Is>(checkers).
                            visitEntry(index, isDelete, before, after)), 0)...}
                };
            });

        // Sean Parent for_each_argument trick
        // (a fold expression with `&&` would be really nice here when we move
        // to C++-17)
        std::array<bool, sizeof...(Is)> finalizers {{
            std::get<Is>(checkers).finalize(tx, terResult, journal)...}};

        // call each check's finalizer to see that it passes
        if (! std::all_of( finalizers.cbegin(), finalizers.cend(),
                [](auto const& b) { return b; }))
        {
            terResult = (terResult == tecINVARIANT_FAILED) ?
                tefINVARIANT_FAILED :
                tecINVARIANT_FAILED ;
            JLOG(journal.error()) <<
                "Transaction has failed one or more invariants: " <<
                to_string(tx.getJson (0));
        }
    }

    return terResult;
}

TER
ApplyContext::checkInvariants(TER terResult)
{
    return checkInvariantsHelper(
        terResult, std::make_index_sequence<std::tuple_size<InvariantChecks>::value>{});
}

} // ripple
