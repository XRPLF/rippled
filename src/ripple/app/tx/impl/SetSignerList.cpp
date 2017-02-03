//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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
#include <ripple/app/tx/impl/SetSignerList.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/basics/Log.h>
#include <cstdint>
#include <algorithm>

namespace ripple {

// We're prepared for there to be multiple signer lists in the future,
// but we don't need them yet.  So for the time being we're manually
// setting the sfSignerListID to zero in all cases.
static std::uint32_t const defaultSignerListID_ = 0;

std::tuple<TER, std::uint32_t,
    std::vector<SignerEntries::SignerEntry>,
        SetSignerList::Operation>
SetSignerList::determineOperation(STTx const& tx,
    ApplyFlags flags, beast::Journal j)
{
    // Check the quorum.  A non-zero quorum means we're creating or replacing
    // the list.  A zero quorum means we're destroying the list.
    auto const quorum = tx[sfSignerQuorum];
    std::vector<SignerEntries::SignerEntry> sign;
    Operation op = unknown;

    bool const hasSignerEntries(tx.isFieldPresent(sfSignerEntries));
    if (quorum && hasSignerEntries)
    {
        auto signers = SignerEntries::deserialize(tx, j, "transaction");

        if (signers.second != tesSUCCESS)
            return std::make_tuple(signers.second, quorum, sign, op);

        std::sort(signers.first.begin(), signers.first.end());

        // Save deserialized list for later.
        sign = std::move(signers.first);
        op = set;
    }
    else if ((quorum == 0) && !hasSignerEntries)
    {
        op = destroy;
    }

    return std::make_tuple(tesSUCCESS, quorum, sign, op);
}

TER
SetSignerList::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureMultiSign))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    auto const result = determineOperation(ctx.tx, ctx.flags, ctx.j);
    if (std::get<0>(result) != tesSUCCESS)
        return std::get<0>(result);

    if (std::get<3>(result) == unknown)
    {
        // Neither a set nor a destroy.  Malformed.
        JLOG(ctx.j.trace()) <<
            "Malformed transaction: Invalid signer set list format.";
        return temMALFORMED;
    }

    if (std::get<3>(result) == set)
    {
        // Validate our settings.
        auto const account = ctx.tx.getAccountID(sfAccount);
        TER const ter =
            validateQuorumAndSignerEntries(std::get<1>(result),
                std::get<2>(result), account, ctx.j);
        if (ter != tesSUCCESS)
        {
            return ter;
        }
    }

    return preflight2 (ctx);
}

TER
SetSignerList::doApply ()
{
    // Perform the operation preCompute() decided on.
    switch (do_)
    {
    case set:
        return replaceSignerList ();

    case destroy:
        return destroySignerList ();

    default:
        break;
    }
    assert (false); // Should not be possible to get here.
    return temMALFORMED;
}

void
SetSignerList::preCompute()
{
    // Get the quorum and operation info.
    auto result = determineOperation(ctx_.tx, view().flags(), j_);
    assert(std::get<0>(result) == tesSUCCESS);
    assert(std::get<3>(result) != unknown);

    quorum_ = std::get<1>(result);
    signers_ = std::get<2>(result);
    do_ = std::get<3>(result);

    return Transactor::preCompute();
}

TER
SetSignerList::validateQuorumAndSignerEntries (
    std::uint32_t quorum,
        std::vector<SignerEntries::SignerEntry> const& signers,
            AccountID const& account,
                beast::Journal j)
{
    // Reject if there are too many or too few entries in the list.
    {
        std::size_t const signerCount = signers.size ();
        if ((signerCount < STTx::minMultiSigners)
            || (signerCount > STTx::maxMultiSigners))
        {
            JLOG(j.trace()) << "Too many or too few signers in signer list.";
            return temMALFORMED;
        }
    }

    // Make sure there are no duplicate signers.
    assert(std::is_sorted(signers.begin(), signers.end()));
    if (std::adjacent_find (
        signers.begin (), signers.end ()) != signers.end ())
    {
        JLOG(j.trace()) << "Duplicate signers in signer list";
        return temBAD_SIGNER;
    }

    // Make sure no signers reference this account.  Also make sure the
    // quorum can be reached.
    std::uint64_t allSignersWeight (0);
    for (auto const& signer : signers)
    {
        std::uint32_t const weight = signer.weight;
        if (weight <= 0)
        {
            JLOG(j.trace()) << "Every signer must have a positive weight.";
            return temBAD_WEIGHT;
        }

        allSignersWeight += signer.weight;

        if (signer.account == account)
        {
            JLOG(j.trace()) << "A signer may not self reference account.";
            return temBAD_SIGNER;
        }

        // Don't verify that the signer accounts exist.  Non-existent accounts
        // may be phantom accounts (which are permitted).
    }
    if ((quorum <= 0) || (allSignersWeight < quorum))
    {
        JLOG(j.trace()) << "Quorum is unreachable";
        return temBAD_QUORUM;
    }
    return tesSUCCESS;
}

TER
SetSignerList::replaceSignerList ()
{
    auto const accountKeylet = keylet::account (account_);
    auto const ownerDirKeylet = keylet::ownerDir (account_);
    auto const signerListKeylet = keylet::signers (account_);

    // This may be either a create or a replace.  Preemptively remove any
    // old signer list.  May reduce the reserve, so this is done before
    // checking the reserve.
    if (TER const ter = removeSignersFromLedger (
        accountKeylet, ownerDirKeylet, signerListKeylet))
            return ter;

    auto const sle = view().peek(accountKeylet);

    // Compute new reserve.  Verify the account has funds to meet the reserve.
    auto const oldOwnerCount = (*sle)[sfOwnerCount];
    std::uint32_t const addedOwnerCount = ownerCountDelta (signers_.size ());

    auto const newReserve =
        view().fees().accountReserve(oldOwnerCount + addedOwnerCount);

    // We check the reserve against the starting balance because we want to
    // allow dipping into the reserve to pay fees.  This behavior is consistent
    // with CreateTicket.
    if (mPriorBalance < newReserve)
        return tecINSUFFICIENT_RESERVE;

    // Everything's ducky.  Add the ltSIGNER_LIST to the ledger.
    auto signerList = std::make_shared<SLE>(signerListKeylet);
    view().insert (signerList);
    writeSignersToSLE (signerList);

    auto viewJ = ctx_.app.journal ("View");
    // Add the signer list to the account's directory.
    std::uint64_t hint;

    auto result = dirAdd(ctx_.view (), hint, ownerDirKeylet,
        signerListKeylet.key, describeOwnerDir (account_), viewJ);

    JLOG(j_.trace()) << "Create signer list for account " <<
        toBase58(account_) << ": " << transHuman (result.first);

    if (result.first == tesSUCCESS)
    {
        signerList->setFieldU64 (sfOwnerNode, hint);

        // If we succeeded, the new entry counts against the
        // creator's reserve.
        adjustOwnerCount(view(), sle, addedOwnerCount, viewJ);
    }

    return result.first;
}

TER
SetSignerList::destroySignerList ()
{
    auto const accountKeylet = keylet::account (account_);
    // Destroying the signer list is only allowed if either the master key
    // is enabled or there is a regular key.
    SLE::pointer ledgerEntry = view().peek (accountKeylet);
    if ((ledgerEntry->isFlag (lsfDisableMaster)) &&
        (!ledgerEntry->isFieldPresent (sfRegularKey)))
            return tecNO_ALTERNATIVE_KEY;

    auto const ownerDirKeylet = keylet::ownerDir (account_);
    auto const signerListKeylet = keylet::signers (account_);
    return removeSignersFromLedger(
        accountKeylet, ownerDirKeylet, signerListKeylet);
}

TER
SetSignerList::removeSignersFromLedger (Keylet const& accountKeylet,
        Keylet const& ownerDirKeylet, Keylet const& signerListKeylet)
{
    // We have to examine the current SignerList so we know how much to
    // reduce the OwnerCount.
    SLE::pointer signers = view().peek (signerListKeylet);

    // If the signer list doesn't exist we've already succeeded in deleting it.
    if (!signers)
        return tesSUCCESS;

    STArray const& actualList = signers->getFieldArray (sfSignerEntries);
    int const removeFromOwnerCount = ownerCountDelta (actualList.size()) * -1;

    // Remove the node from the account directory.
    auto const hint = (*signers)[sfOwnerNode];

    auto viewJ = ctx_.app.journal ("View");
    TER const result  = dirDelete(ctx_.view(), false, hint,
        ownerDirKeylet.key, signerListKeylet.key, false, (hint == 0), viewJ);

    if (result == tesSUCCESS)
        adjustOwnerCount(view(),
            view().peek(accountKeylet), removeFromOwnerCount, viewJ);

    ctx_.view().erase (signers);

    return result;
}

void
SetSignerList::writeSignersToSLE (SLE::pointer const& ledgerEntry) const
{
    // Assign the quorum.
    ledgerEntry->setFieldU32 (sfSignerQuorum, quorum_);

    // For now, assign the default SignerListID.
    ledgerEntry->setFieldU32 (sfSignerListID, defaultSignerListID_);

    // Create the SignerListArray one SignerEntry at a time.
    STArray toLedger (signers_.size ());
    for (auto const& entry : signers_)
    {
        toLedger.emplace_back(sfSignerEntry);
        STObject& obj = toLedger.back();
        obj.reserve (2);
        obj.setAccountID (sfAccount, entry.account);
        obj.setFieldU16 (sfSignerWeight, entry.weight);
    }

    // Assign the SignerEntries.
    ledgerEntry->setFieldArray (sfSignerEntries, toLedger);
}

// The return type is signed so it is compatible with the 3rd argument
// of adjustOwnerCount() (which must be signed).
int
SetSignerList::ownerCountDelta (std::size_t entryCount)
{
    // We always compute the full change in OwnerCount, taking into account:
    //  o The fact that we're adding/removing a SignerList and
    //  o Accounting for the number of entries in the list.
    // We can get away with that because lists are not adjusted incrementally;
    // we add or remove an entire list.
    //
    // The rule is:
    //  o Simply having a SignerList costs 2 OwnerCount units.
    //  o And each signer in the list costs 1 more OwnerCount unit.
    // So, at a minimum, adding a SignerList with 1 entry costs 3 OwnerCount
    // units.  A SignerList with 8 entries would cost 10 OwnerCount units.
    //
    // The static_cast should always be safe since entryCount should always
    // be in the range from 1 to 8.  We've got a lot of room to grow.
    assert (entryCount >= STTx::minMultiSigners);
    assert (entryCount <= STTx::maxMultiSigners);
    return 2 + static_cast<int>(entryCount);
}

} // namespace ripple
