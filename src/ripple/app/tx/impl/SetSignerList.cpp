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

/** Compute the owner count change for the given number of singer entries

    The "MultiSignReserve" amendment changed how this is calculated but
    this logic is needed to property adjust owner counts of pre-existing
    lists.
*/
static int
signerCountBasedOwnerCountDelta(std::size_t entryCount)
{
    assert(entryCount >= STTx::minMultiSigners);
    assert(entryCount <= STTx::maxMultiSigners);
    return 2 + static_cast<int>(entryCount);
}

// We're prepared for there to be multiple signer lists in the future,
// but we don't need them yet.  So for the time being we're manually
// setting the sfSignerListID to zero in all cases.
static std::uint32_t const defaultSignerListID_ = 0;

std::tuple<
    NotTEC,
    std::uint32_t,
    std::vector<SignerEntry>,
    SetSignerList::Operation>
SetSignerList::determineOperation(STTx const& tx)
{
    // Check the quorum.  A non-zero quorum means we're creating or replacing
    // the list.  A zero quorum means we're destroying the list.
    auto const quorum = tx[sfSignerQuorum];
    std::vector<SignerEntry> sign;
    Operation op = unknown;

    bool const hasSignerEntries(tx.isFieldPresent(sfSignerEntries));
    if (quorum && hasSignerEntries)
    {
        auto signers = deserializeSignerList(tx);

        if (!signers)
            return std::make_tuple(temMALFORMED, quorum, sign, op);

        std::sort(signers->begin(), signers->end());

        // Save deserialized list for later.
        sign = std::move(*signers);
        op = set;
    }
    else if ((quorum == 0) && !hasSignerEntries)
    {
        op = destroy;
    }

    return std::make_tuple(tesSUCCESS, quorum, sign, op);
}

NotTEC
SetSignerList::preflight(PreflightContext const& ctx)
{
    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.rules.enabled(featureSimplifiedSetSignerList) &&
        ctx.rules.enabled(featureMultiSignReserve))
    {
        auto const quorum = ctx.tx[sfSignerQuorum];

        // The quorum says when we are adding or removing the signer list:
        if (quorum != 0)
        {
            auto const sl = deserializeSignerList(ctx.tx);

            if (!sl)
                return temMALFORMED;

            if (sl->size() < STTx::minMultiSigners ||
                sl->size() > STTx::maxMultiSigners)
                return temMALFORMED;

            if (!std::is_sorted(sl->begin(), sl->end()))
                return temMALFORMED;

            if (auto const dup = std::adjacent_find(sl->begin(), sl->end());
                dup != sl->end())
                return temBAD_SIGNER;

            auto const self = ctx.tx.getAccountID(sfAccount);
            std::uint32_t weight = 0;

            for (auto const& s : *sl)
            {
                if (s.account == self)
                    return temBAD_SIGNER;

                if (s.weight == 0)
                    return temBAD_WEIGHT;

                weight += s.weight;
            }

            if (weight < quorum)
                return temBAD_QUORUM;
        }

        if (quorum == 0 && ctx.tx.isFieldPresent(sfSignerEntries))
            return temMALFORMED;
    }
    else
    {
        auto const result = determineOperation(ctx.tx);
        if (std::get<0>(result) != tesSUCCESS)
            return std::get<0>(result);

        if (std::get<3>(result) == unknown)
        {
            // Neither a set nor a destroy.  Malformed.
            JLOG(ctx.j.trace())
                << "Malformed transaction: Invalid signer set list format.";
            return temMALFORMED;
        }

        if (std::get<3>(result) == set)
        {
            // Validate our settings.
            auto const account = ctx.tx.getAccountID(sfAccount);
            NotTEC const ter = validateQuorumAndSignerEntries(
                std::get<1>(result), std::get<2>(result), account, ctx.j);
            if (ter != tesSUCCESS)
            {
                return ter;
            }
        }
    }

    return preflight2(ctx);
}

TER
SetSignerList::doApply()
{
    if (ctx_.view().rules().enabled(featureSimplifiedSetSignerList) &&
        ctx_.view().rules().enabled(featureMultiSignReserve))
    {
        auto const quorum = ctx_.tx[sfSignerQuorum];

        // If the list is removed check that the account can still sign the
        // transactions:
        if (quorum == 0)
        {
            auto acct = view().peek(keylet::account(account_));

            // Destroying the signer list is only allowed if either the master
            // key is enabled or there is a regular key.
            if (acct->isFlag(lsfDisableMaster) &&
                !acct->isFieldPresent(sfRegularKey))
                return tecNO_ALTERNATIVE_KEY;
        }

        auto const slk = keylet::signers(account_);
        auto const odk = keylet::ownerDir(account_);

        auto const root = view().peek(keylet::account(account_));

        // Remove any existing signer list
        if (auto sle = view().peek(slk))
        {
            int ocdelta = 1;

            if ((sle->getFlags() & lsfOneOwnerCount) == 0)
                ocdelta = signerCountBasedOwnerCountDelta(
                    sle->getFieldArray(sfSignerEntries).size());

            if (!ctx_.view().dirRemove(odk, (*sle)[sfOwnerNode], slk, false))
                return tefBAD_LEDGER;

            ctx_.view().erase(sle);

            adjustOwnerCount(view(), root, -ocdelta, ctx_.journal);
        }

        if (quorum != 0)
        {
            int const ocdelta = 1;

            // We check the reserve against the starting balance because we want
            // to allow dipping into the reserve to pay fees.
            if (mPriorBalance <
                view().fees().accountReserve(ocdelta + (*root)[sfOwnerCount]))
                return tecINSUFFICIENT_RESERVE;

            auto sle = std::make_shared<SLE>(slk);
            sle->setFieldU32(sfSignerQuorum, quorum);
            sle->setFieldU32(sfSignerListID, defaultSignerListID_);
            sle->setFieldU32(sfFlags, lsfOneOwnerCount);
            sle->setFieldArray(
                sfSignerEntries, ctx_.tx.getFieldArray(sfSignerEntries));

            view().insert(sle);

            auto const page =
                ctx_.view().dirInsert(odk, slk, describeOwnerDir(account_));

            if (!page)
                return tecDIR_FULL;

            sle->setFieldU64(sfOwnerNode, *page);

            adjustOwnerCount(view(), root, ocdelta, ctx_.journal);
        }

        return tesSUCCESS;
    }

    // Perform the operation preCompute() decided on.
    switch (do_)
    {
        case set:
            return replaceSignerList();

        case destroy:
            return destroySignerList();

        default:
            break;
    }
    assert(false);  // Should not be possible to get here.
    return temMALFORMED;
}

void
SetSignerList::preCompute()
{
    if (!(ctx_.view().rules().enabled(featureSimplifiedSetSignerList) &&
          ctx_.view().rules().enabled(featureMultiSignReserve)))
    {
        // Get the quorum and operation info.
        auto result = determineOperation(ctx_.tx);
        assert(std::get<0>(result) == tesSUCCESS);
        assert(std::get<3>(result) != unknown);

        quorum_ = std::get<1>(result);
        signers_ = std::get<2>(result);
        do_ = std::get<3>(result);
    }

    return Transactor::preCompute();
}

static TER
removeSignersFromLedger(
    Application& app,
    ApplyView& view,
    Keylet const& accountKeylet,
    Keylet const& ownerDirKeylet,
    Keylet const& signerListKeylet)
{
    // We have to examine the current SignerList so we know how much to
    // reduce the OwnerCount.
    SLE::pointer signers = view.peek(signerListKeylet);

    // If the signer list doesn't exist we've already succeeded in deleting it.
    if (!signers)
        return tesSUCCESS;

    // There are two different ways that the OwnerCount could be managed.
    // If the lsfOneOwnerCount bit is set then remove just one owner count.
    // Otherwise use the pre-MultiSignReserve amendment calculation.
    int removeFromOwnerCount = -1;
    if ((signers->getFlags() & lsfOneOwnerCount) == 0)
    {
        STArray const& actualList = signers->getFieldArray(sfSignerEntries);
        removeFromOwnerCount =
            signerCountBasedOwnerCountDelta(actualList.size()) * -1;
    }

    // Remove the node from the account directory.
    auto const hint = (*signers)[sfOwnerNode];

    if (!view.dirRemove(ownerDirKeylet, hint, signerListKeylet.key, false))
    {
        return tefBAD_LEDGER;
    }

    adjustOwnerCount(
        view,
        view.peek(accountKeylet),
        removeFromOwnerCount,
        app.journal("View"));

    view.erase(signers);

    return tesSUCCESS;
}

TER
SetSignerList::removeFromLedger(
    Application& app,
    ApplyView& view,
    AccountID const& account)
{
    auto const accountKeylet = keylet::account(account);
    auto const ownerDirKeylet = keylet::ownerDir(account);
    auto const signerListKeylet = keylet::signers(account);

    return removeSignersFromLedger(
        app, view, accountKeylet, ownerDirKeylet, signerListKeylet);
}

NotTEC
SetSignerList::validateQuorumAndSignerEntries(
    std::uint32_t quorum,
    std::vector<SignerEntry> const& signers,
    AccountID const& account,
    beast::Journal j)
{
    // Reject if there are too many or too few entries in the list.
    {
        std::size_t const signerCount = signers.size();
        if ((signerCount < STTx::minMultiSigners) ||
            (signerCount > STTx::maxMultiSigners))
        {
            JLOG(j.trace()) << "Too many or too few signers in signer list.";
            return temMALFORMED;
        }
    }

    // Make sure there are no duplicate signers.
    assert(std::is_sorted(signers.begin(), signers.end()));
    if (std::adjacent_find(signers.begin(), signers.end()) != signers.end())
    {
        JLOG(j.trace()) << "Duplicate signers in signer list";
        return temBAD_SIGNER;
    }

    // Make sure no signers reference this account.  Also make sure the
    // quorum can be reached.
    std::uint64_t allSignersWeight(0);
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
SetSignerList::replaceSignerList()
{
    auto const accountKeylet = keylet::account(account_);
    auto const ownerDirKeylet = keylet::ownerDir(account_);
    auto const signerListKeylet = keylet::signers(account_);

    // This may be either a create or a replace.  Preemptively remove any
    // old signer list.  May reduce the reserve, so this is done before
    // checking the reserve.
    if (TER const ter = removeSignersFromLedger(
            ctx_.app, view(), accountKeylet, ownerDirKeylet, signerListKeylet))
        return ter;

    auto const sle = view().peek(accountKeylet);
    if (!sle)
        return tefINTERNAL;

    // Compute new reserve.  Verify the account has funds to meet the reserve.
    std::uint32_t const oldOwnerCount{(*sle)[sfOwnerCount]};

    // The required reserve changes based on featureMultiSignReserve...
    int addedOwnerCount{1};
    std::uint32_t flags{lsfOneOwnerCount};
    if (!ctx_.view().rules().enabled(featureMultiSignReserve))
    {
        addedOwnerCount = signerCountBasedOwnerCountDelta(signers_.size());
        flags = 0;
    }

    XRPAmount const newReserve{
        view().fees().accountReserve(oldOwnerCount + addedOwnerCount)};

    // We check the reserve against the starting balance because we want to
    // allow dipping into the reserve to pay fees.  This behavior is consistent
    // with CreateTicket.
    if (mPriorBalance < newReserve)
        return tecINSUFFICIENT_RESERVE;

    // Everything's ducky.  Add the ltSIGNER_LIST to the ledger.
    auto signerList = std::make_shared<SLE>(signerListKeylet);
    view().insert(signerList);
    writeSignersToSLE(signerList, flags);

    auto viewJ = ctx_.app.journal("View");
    // Add the signer list to the account's directory.
    auto const page = dirAdd(
        ctx_.view(),
        ownerDirKeylet,
        signerListKeylet.key,
        false,
        describeOwnerDir(account_),
        viewJ);

    JLOG(j_.trace()) << "Create signer list for account " << toBase58(account_)
                     << ": " << (page ? "success" : "failure");

    if (!page)
        return tecDIR_FULL;

    signerList->setFieldU64(sfOwnerNode, *page);

    // If we succeeded, the new entry counts against the
    // creator's reserve.
    adjustOwnerCount(view(), sle, addedOwnerCount, viewJ);
    return tesSUCCESS;
}

TER
SetSignerList::destroySignerList()
{
    auto const accountKeylet = keylet::account(account_);
    // Destroying the signer list is only allowed if either the master key
    // is enabled or there is a regular key.
    SLE::pointer ledgerEntry = view().peek(accountKeylet);
    if (!ledgerEntry)
        return tefINTERNAL;

    if ((ledgerEntry->isFlag(lsfDisableMaster)) &&
        (!ledgerEntry->isFieldPresent(sfRegularKey)))
        return tecNO_ALTERNATIVE_KEY;

    auto const ownerDirKeylet = keylet::ownerDir(account_);
    auto const signerListKeylet = keylet::signers(account_);
    return removeSignersFromLedger(
        ctx_.app, view(), accountKeylet, ownerDirKeylet, signerListKeylet);
}

void
SetSignerList::writeSignersToSLE(
    SLE::pointer const& ledgerEntry,
    std::uint32_t flags) const
{
    // Assign the quorum, default SignerListID, and flags.
    ledgerEntry->setFieldU32(sfSignerQuorum, quorum_);
    ledgerEntry->setFieldU32(sfSignerListID, defaultSignerListID_);
    if (flags)  // Only set flags if they are non-default (default is zero).
        ledgerEntry->setFieldU32(sfFlags, flags);

    // Create the SignerListArray one SignerEntry at a time.
    STArray toLedger(signers_.size());
    for (auto const& entry : signers_)
    {
        toLedger.emplace_back(sfSignerEntry);
        STObject& obj = toLedger.back();
        obj.reserve(2);
        obj.setAccountID(sfAccount, entry.account);
        obj.setFieldU16(sfSignerWeight, entry.weight);
    }

    // Assign the SignerEntries.
    ledgerEntry->setFieldArray(sfSignerEntries, toLedger);
}

}  // namespace ripple
