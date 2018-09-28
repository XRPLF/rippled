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

#include <ripple/app/tx/impl/SetSignerList.h>

#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/ApplyView.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/STTx.h>
#include <algorithm>
#include <cstdint>

namespace ripple {

// We're prepared for there to be multiple signer lists in the future,
// but we don't need them yet.  So for the time being we're manually
// setting the sfSignerListID to zero in all cases.
static std::uint32_t const defaultSignerListID_ = 0;

std::tuple<NotTEC, std::uint32_t,
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

NotTEC
SetSignerList::preflight (PreflightContext const& ctx)
{
    if (! ctx.rules.enabled(featureMultiSign))
        return temDISABLED;

    auto const ret = preflight1 (ctx);
    if (!isTesSuccess (ret))
        return ret;

    if (ctx.rules.enabled(featureNoGoodName))
    {
        auto const quorum = ctx.tx[sfSignerQuorum];

        // If quorum is non-zero we are adding a signer list so one
        // must be specified; otherwise we are removing a signer list
        // so no singer list must be present.
        if (quorum != 0)
        {
            if (!ctx.tx.isFieldPresent(sfSignerEntries))
                return temMALFORMED;

            auto const size = ctx.tx.getFieldArray(sfSignerEntries).size();

            if (size < STTx::minMultiSigners || size > STTx::maxMultiSigners)
                return temMALFORMED;
        }

        if (quorum == 0 && ctx.tx.isFieldPresent(sfSignerEntries))
            return temMALFORMED;
    }
    else
    {
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
            NotTEC const ter =
                validateQuorumAndSignerEntries(std::get<1>(result),
                                               std::get<2>(result), account, ctx.j);
            if (ter != tesSUCCESS)
            {
                return ter;
            }
        }
    }

    return preflight2 (ctx);
}

bool
SetSignerList::extract(STArray const& entries,
    std::vector<std::pair<AccountID, std::uint16_t>>& result)
{
    if (entries.size() != 0)
    {
        result.clear();
        result.reserve(entries.size());

        for (auto const& e : entries)
        {
            if (e.getFName() != sfSignerEntry)
                return false;
            result.emplace_back(e.getAccountID(sfAccount), e.getFieldU16(sfSignerWeight));
        }
    }

    // We rely on the default std::pair comparator here, so duplicate entries
    // will be sorted by weight.
    std::sort(result.begin(), result.end());

    return true;
}

TER
SetSignerList::doApply ()
{
    if (ctx_.view().rules().enabled(featureNoGoodName))
    {
        // If the list is removed check that the account will still be able
        // to sign transactions:
        if (quorum_ == 0)
        {
            auto acct = view().peek(keylet::account(account_));

            // Destroying the signer list is only allowed if either the master
            // key is enabled or there is a regular key.
            if (acct->isFlag(lsfDisableMaster) && !acct->isFieldPresent(sfRegularKey))
                return tecNO_ALTERNATIVE_KEY;
        }

        auto const slk = keylet::signers (account_);
        auto const odk = keylet::ownerDir(account_);

        auto const root = view().peek(keylet::account(account_));

        // At this pint we want to remove the existing signer list if one is
        // present:
        if (auto sl = view().peek(slk))
        {
            int ocdelta = 1;

            if ((sl->getFlags() & lsfOneOwnerCount) == 0)
                ocdelta = legacyOwnerCountDelta(
                    sl->getFieldArray(sfSignerEntries).size());

            // Remove the node from the account directory.
            auto const hint = (*sl)[sfOwnerNode];

            if (!ctx_.view().dirRemove(odk, hint, slk, false))
                return tefBAD_LEDGER;

            adjustOwnerCount(view(), root, -ocdelta, {});
            ctx_.view().erase(sl);
        }

        // If we are adding a new list do it now:
        if (quorum_ != 0)
        {
            std::vector<std::pair<AccountID, std::uint16_t>> signers;

            if (!extract(ctx_.tx.getFieldArray(sfSignerEntries), signers))
                return tecBAD_SIGNER_LIST;

            if (signers.empty())
                return tecBAD_SIGNER_LIST;

            // Ensure that no account appears twice in the list:
            auto const dup = std::adjacent_find(signers.begin(), signers.end(),
                [](auto const& a, auto const& b) { return (a.first == b.first); });

            if (dup != signers.end())
                return tecBAD_SIGNER_LIST;

            // Calculate the maximum weight of this list, and make sure that it
            // doesn't reference the account that it's being set against and
            // ensure all entries have non-zero weights:
            std::uint32_t weight = 0;

            for (auto s : signers)
            {
                if (s.first == account_)
                    return tecBAD_SIGNER_LIST;

                if (s.second == 0)
                    return tecBAD_SIGNER_LIST;

                weight += s.second;
            }

            if (weight < quorum_)
                return tecBAD_QUORUM;

            // The required reserve changes based on featureMultiSignReserve...
            std::uint32_t flags = lsfOneOwnerCount;
            int ocdelta = 1;

            if (! ctx_.view().rules().enabled(featureMultiSignReserve))
            {
                ocdelta = legacyOwnerCountDelta (signers.size ());
                flags = 0;
            }

            // We check the reserve against the starting balance because we want
            // to allow dipping into the reserve to pay fees.
            if (mPriorBalance < view().fees().accountReserve(ocdelta + (*root)[sfOwnerCount]))
                return tecINSUFFICIENT_RESERVE;

            STArray toLedger (signers.size());

            for (auto const& entry : signers)
            {
                toLedger.emplace_back(sfSignerEntry);
                STObject& obj = toLedger.back();
                obj.reserve (2);
                obj.setAccountID (sfAccount, entry.first);
                obj.setFieldU16 (sfSignerWeight, entry.second);
            }

            auto sl = std::make_shared<SLE>(slk);
            sl->setFieldU32 (sfSignerQuorum, quorum_);
            sl->setFieldU32 (sfSignerListID, defaultSignerListID_);
            sl->setFieldArray (sfSignerEntries, toLedger);
            if (flags)
                sl->setFieldU32 (sfFlags, flags);

            view().insert (sl);

            auto const page = ctx_.view().dirInsert(odk, slk, describeOwnerDir(account_));

            if (!page)
                return tecDIR_FULL;

            sl->setFieldU64(sfOwnerNode, *page);

            adjustOwnerCount(view(), root, ocdelta, {});
        }

        return tesSUCCESS;
    }

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
    if (ctx_.view().rules().enabled(featureNoGoodName))
    {
        quorum_ = ctx_.tx[sfSignerQuorum];
    }
    else
    {
        // Get the quorum and operation info.
        auto result = determineOperation(ctx_.tx, view().flags(), j_);
        assert(std::get<0>(result) == tesSUCCESS);
        assert(std::get<3>(result) != unknown);

        quorum_ = std::get<1>(result);
        signers_ = std::get<2>(result);
        do_ = std::get<3>(result);
    }

    return Transactor::preCompute();
}

NotTEC
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
    std::uint32_t const oldOwnerCount {(*sle)[sfOwnerCount]};

    // The required reserve changes based on featureMultiSignReserve...
    int addedOwnerCount {1};
    std::uint32_t flags {lsfOneOwnerCount};
    if (! ctx_.view().rules().enabled(featureMultiSignReserve))
    {
        addedOwnerCount = legacyOwnerCountDelta (signers_.size ());
        flags = 0;
    }

    XRPAmount const newReserve {
        view().fees().accountReserve(oldOwnerCount + addedOwnerCount)};

    // We check the reserve against the starting balance because we want to
    // allow dipping into the reserve to pay fees.  This behavior is consistent
    // with CreateTicket.
    if (mPriorBalance < newReserve)
        return tecINSUFFICIENT_RESERVE;

    // Everything's ducky.  Add the ltSIGNER_LIST to the ledger.
    auto signerList = std::make_shared<SLE>(signerListKeylet);
    view().insert (signerList);
    writeSignersToSLE (signerList, flags);

    auto viewJ = ctx_.app.journal ("View");
    // Add the signer list to the account's directory.
    auto const page = dirAdd (ctx_.view (), ownerDirKeylet,
        signerListKeylet.key, false, describeOwnerDir (account_), viewJ);

    JLOG(j_.trace()) << "Create signer list for account " <<
        toBase58 (account_) << ": " << (page ? "success" : "failure");

    if (!page)
        return tecDIR_FULL;

    signerList->setFieldU64 (sfOwnerNode, *page);

    // If we succeeded, the new entry counts against the
    // creator's reserve.
    adjustOwnerCount(view(), sle, addedOwnerCount, viewJ);
    return tesSUCCESS;
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

    // There are two different ways that the OwnerCount could be managed.
    // If the lsfOneOwnerCount bit is set then remove just one owner count.
    // Otherwise use the pre-MultiSignReserve amendment calculation.
    int removeFromOwnerCount = -1;
    if ((signers->getFlags() & lsfOneOwnerCount) == 0)
    {
        STArray const& actualList = signers->getFieldArray (sfSignerEntries);
        removeFromOwnerCount = legacyOwnerCountDelta (actualList.size()) * -1;
    }

    // Remove the node from the account directory.
    auto const hint = (*signers)[sfOwnerNode];

    if (! ctx_.view().dirRemove(
            ownerDirKeylet, hint, signerListKeylet.key, false))
    {
        return tefBAD_LEDGER;
    }

    auto viewJ = ctx_.app.journal("View");
    adjustOwnerCount(
        view(), view().peek(accountKeylet), removeFromOwnerCount, viewJ);

    ctx_.view().erase (signers);

    return tesSUCCESS;
}

void
SetSignerList::writeSignersToSLE (
    SLE::pointer const& ledgerEntry, std::uint32_t flags) const
{
    // Assign the quorum, default SignerListID, and flags.
    ledgerEntry->setFieldU32 (sfSignerQuorum, quorum_);
    ledgerEntry->setFieldU32 (sfSignerListID, defaultSignerListID_);
    if (flags) // Only set flags if they are non-default (default is zero).
        ledgerEntry->setFieldU32 (sfFlags, flags);

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

} // namespace ripple
