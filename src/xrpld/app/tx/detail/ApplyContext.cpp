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

#include <xrpld/app/tx/detail/ApplyContext.h>
#include <xrpld/app/tx/detail/InvariantCheck.h>
#include <xrpld/app/tx/detail/Transactor.h>
#include <xrpl/basics/Log.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <cassert>

namespace ripple {

ApplyContext::ApplyContext(
    Application& app_,
    OpenView& base,
    STTx const& tx_,
    TER preclaimResult_,
    XRPAmount baseFee_,
    ApplyFlags flags,
    beast::Journal journal_)
    : app(app_)
    , tx(tx_)
    , preclaimResult(preclaimResult_)
    , baseFee(baseFee_)
    , journal(journal_)
    , base_(base)
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

/**
 * Applies the changes in the given OpenView to the ApplyContext's base.
 * If the base is not open, the changes in the OpenView are directly applied to
 * the base.
 *
 * @param open The OpenView containing the changes to be applied.
 */
void
ApplyContext::applyOpenView(OpenView& open)
{
    open.apply(base_);
}

/**
 * Update the AccountRoot ledger entry associated with the batch transaction
 * to ensure that the final entry accurately reflects all modifications made
 * by inner transactions that affect the same account.
 *
 * This function retrieves the current AccountRoot entry for the account
 * associated with the batch transaction and replaces it in the view.
 * This is necessary because inner transactions are processed first, and
 * their changes may impact the overall entry of the account. By updating
 * the AccountRoot entry, we ensure that any changes made by these inner
 * transactions are accounted for in the final entry of the batch transaction.
 */
void
ApplyContext::updateAccountRootEntry()
{
    AccountID const account = tx.getAccountID(sfAccount);
    auto const sleBase = base_.read(keylet::account(account));
    if (sleBase)
        view_->rawReplace(std::make_shared<SLE>(*sleBase));
}

/**
 * Capture the previous state of the AccountRoot ledger entry associated
 * with the batch transaction before applying inner transactions. This
 * function retrieves the current AccountRoot entry and prepares metadata
 * that reflects any changes made by inner transactions that may affect
 * the account's overall state.
 *  @param avi The ApplyViewImpl instance to which the previous metadata
 *  will be added.
 */
void
ApplyContext::setBatchPrevAcctRootFields(ApplyViewImpl& avi)
{
    AccountID const account = tx.getAccountID(sfAccount);
    auto const sleBaseAcct = base_.read(keylet::account(account));
    auto const sleAcct = view_->peek(keylet::account(account));
    if (sleAcct && sleBaseAcct)
    {
        STObject prevFields{sfPreviousFields};
        for (auto const& obj : *sleBaseAcct)
        {
            if (obj.getFName().shouldMeta(SField::sMD_ChangeOrig) &&
                (!sleAcct->hasMatchingEntry(obj) ||
                 obj.getFName() == sfSequence ||
                 obj.getFName() == sfOwnerCount ||
                 obj.getFName() == sfTicketCount))
            {
                prevFields.emplace_back(obj);
            }
        }
        avi.setBatchPrevMetaData(std::move(prevFields));
    }
}

std::size_t
ApplyContext::size()
{
    return view_->size();
}

void
ApplyContext::visit(std::function<void(
                        uint256 const&,
                        bool,
                        std::shared_ptr<SLE const> const&,
                        std::shared_ptr<SLE const> const&)> const& func)
{
    view_->visit(base_, func);
}

TER
ApplyContext::failInvariantCheck(TER const result)
{
    // If we already failed invariant checks before and we are now attempting to
    // only charge a fee, and even that fails the invariant checks something is
    // very wrong. We switch to tefINVARIANT_FAILED, which does NOT get included
    // in a ledger.

    return (result == tecINVARIANT_FAILED || result == tefINVARIANT_FAILED)
        ? TER{tefINVARIANT_FAILED}
        : TER{tecINVARIANT_FAILED};
}

template <std::size_t... Is>
TER
ApplyContext::checkInvariantsHelper(
    TER const result,
    XRPAmount const fee,
    std::index_sequence<Is...>)
{
    try
    {
        auto checkers = getInvariantChecks();

        // call each check's per-entry method
        visit([&checkers](
                  uint256 const& index,
                  bool isDelete,
                  std::shared_ptr<SLE const> const& before,
                  std::shared_ptr<SLE const> const& after) {
            (..., std::get<Is>(checkers).visitEntry(isDelete, before, after));
        });

        // Note: do not replace this logic with a `...&&` fold expression.
        // The fold expression will only run until the first check fails (it
        // short-circuits). While the logic is still correct, the log
        // message won't be. Every failed invariant should write to the log,
        // not just the first one.
        std::array<bool, sizeof...(Is)> finalizers{
            {std::get<Is>(checkers).finalize(
                tx, result, fee, *view_, journal)...}};

        // call each check's finalizer to see that it passes
        if (!std::all_of(
                finalizers.cbegin(), finalizers.cend(), [](auto const& b) {
                    return b;
                }))
        {
            JLOG(journal.fatal())
                << "Transaction has failed one or more invariants: "
                << to_string(tx.getJson(JsonOptions::none));

            return failInvariantCheck(result);
        }
    }
    catch (std::exception const& ex)
    {
        JLOG(journal.fatal())
            << "Transaction caused an exception in an invariant"
            << ", ex: " << ex.what()
            << ", tx: " << to_string(tx.getJson(JsonOptions::none));

        return failInvariantCheck(result);
    }

    return result;
}

TER
ApplyContext::checkInvariants(TER const result, XRPAmount const fee)
{
    assert(isTesSuccess(result) || isTecClaim(result));

    return checkInvariantsHelper(
        result,
        fee,
        std::make_index_sequence<std::tuple_size<InvariantChecks>::value>{});
}

}  // namespace ripple
