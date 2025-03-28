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

#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/tx/detail/SetSignerList.h>
#include <xrpld/ledger/ApplyView.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxFlags.h>

#include <algorithm>
#include <cstdint>

namespace ripple {

// We're prepared for there to be multiple signer lists in the future,
// but we don't need them yet.  So for the time being we're manually
// setting the sfSignerListID to zero in all cases.
static std::uint32_t const defaultSignerListID_ = 0;

std::tuple<
    NotTEC,
    std::uint32_t,
    std::vector<SignerEntries::SignerEntry>,
    SetSignerList::Operation>
SetSignerList::determineOperation(
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
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

        if (!signers)
            return std::make_tuple(signers.error(), quorum, sign, op);

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

    if (ctx.rules.enabled(fixInvalidTxFlags) &&
        (ctx.tx.getFlags() & tfUniversalMask))
    {
        JLOG(ctx.j.debug()) << "SetSignerList: invalid flags.";
        return temINVALID_FLAG;
    }

    auto const result = determineOperation(ctx.tx, ctx.flags, ctx.j);

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
            std::get<1>(result),
            std::get<2>(result),
            account,
            ctx.j,
            ctx.rules);
        if (ter != tesSUCCESS)
        {
            return ter;
        }
    }

    return preflight2(ctx);
}

TER
SetSignerList::doApply()
{
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
    UNREACHABLE("ripple::SetSignerList::doApply : invalid operation");
    return temMALFORMED;
}

void
SetSignerList::preCompute()
{
    // Get the quorum and operation info.
    auto result = determineOperation(ctx_.tx, view().flags(), j_);
    XRPL_ASSERT(
        std::get<0>(result) == tesSUCCESS,
        "ripple::SetSignerList::preCompute : result is tesSUCCESS");
    XRPL_ASSERT(
        std::get<3>(result) != unknown,
        "ripple::SetSignerList::preCompute : result is known operation");

    quorum_ = std::get<1>(result);
    signers_ = std::get<2>(result);
    do_ = std::get<3>(result);

    return Transactor::preCompute();
}

// The return type is signed so it is compatible with the 3rd argument
// of adjustOwnerCount() (which must be signed).
//
// NOTE: This way of computing the OwnerCount associated with a SignerList
// is valid until the featureMultiSignReserve amendment passes.  Once it
// passes then just 1 OwnerCount is associated with a SignerList.
static int
signerCountBasedOwnerCountDelta(std::size_t entryCount, Rules const& rules)
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
    // be in the range from 1 to 8 (or 32 if ExpandedSignerList is enabled).
    // We've got a lot of room to grow.
    XRPL_ASSERT(
        entryCount >= STTx::minMultiSigners,
        "ripple::signerCountBasedOwnerCountDelta : minimum signers");
    XRPL_ASSERT(
        entryCount <= STTx::maxMultiSigners(&rules),
        "ripple::signerCountBasedOwnerCountDelta : maximum signers");
    return 2 + static_cast<int>(entryCount);
}

static TER
removeSignersFromLedger(
    Application& app,
    ApplyView& view,
    Keylet const& accountKeylet,
    Keylet const& ownerDirKeylet,
    Keylet const& signerListKeylet,
    beast::Journal j)
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
            signerCountBasedOwnerCountDelta(actualList.size(), view.rules()) *
            -1;
    }

    // Remove the node from the account directory.
    auto const hint = (*signers)[sfOwnerNode];

    if (!view.dirRemove(ownerDirKeylet, hint, signerListKeylet.key, false))
    {
        JLOG(j.fatal()) << "Unable to delete SignerList from owner.";
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
    AccountID const& account,
    beast::Journal j)
{
    auto const accountKeylet = keylet::account(account);
    auto const ownerDirKeylet = keylet::ownerDir(account);
    auto const signerListKeylet = keylet::signers(account);

    return removeSignersFromLedger(
        app, view, accountKeylet, ownerDirKeylet, signerListKeylet, j);
}

NotTEC
SetSignerList::validateQuorumAndSignerEntries(
    std::uint32_t quorum,
    std::vector<SignerEntries::SignerEntry> const& signers,
    AccountID const& account,
    beast::Journal j,
    Rules const& rules)
{
    // Reject if there are too many or too few entries in the list.
    {
        std::size_t const signerCount = signers.size();
        if ((signerCount < STTx::minMultiSigners) ||
            (signerCount > STTx::maxMultiSigners(&rules)))
        {
            JLOG(j.trace()) << "Too many or too few signers in signer list.";
            return temMALFORMED;
        }
    }

    // Make sure there are no duplicate signers.
    XRPL_ASSERT(
        std::is_sorted(signers.begin(), signers.end()),
        "ripple::SetSignerList::validateQuorumAndSignerEntries : sorted "
        "signers");
    if (std::adjacent_find(signers.begin(), signers.end()) != signers.end())
    {
        JLOG(j.trace()) << "Duplicate signers in signer list";
        return temBAD_SIGNER;
    }

    // Is the ExpandedSignerList amendment active?
    bool const expandedSignerList = rules.enabled(featureExpandedSignerList);

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

        if (signer.tag && !expandedSignerList)
        {
            JLOG(j.trace()) << "Malformed transaction: sfWalletLocator "
                               "specified in SignerEntry "
                            << "but featureExpandedSignerList is not enabled.";
            return temMALFORMED;
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
            ctx_.app,
            view(),
            accountKeylet,
            ownerDirKeylet,
            signerListKeylet,
            j_))
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
        addedOwnerCount = signerCountBasedOwnerCountDelta(
            signers_.size(), ctx_.view().rules());
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
    auto const page = ctx_.view().dirInsert(
        ownerDirKeylet, signerListKeylet, describeOwnerDir(account_));

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
        ctx_.app, view(), accountKeylet, ownerDirKeylet, signerListKeylet, j_);
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

    bool const expandedSignerList =
        ctx_.view().rules().enabled(featureExpandedSignerList);

    // Create the SignerListArray one SignerEntry at a time.
    STArray toLedger(signers_.size());
    for (auto const& entry : signers_)
    {
        toLedger.push_back(STObject::makeInnerObject(sfSignerEntry));
        STObject& obj = toLedger.back();
        obj.reserve(2);
        obj[sfAccount] = entry.account;
        obj[sfSignerWeight] = entry.weight;

        // This is a defensive check to make absolutely sure we will never write
        // a tag into the ledger while featureExpandedSignerList is not enabled
        if (expandedSignerList && entry.tag)
            obj.setFieldH256(sfWalletLocator, *(entry.tag));
    }

    // Assign the SignerEntries.
    ledgerEntry->setFieldArray(sfSignerEntries, toLedger);
}

}  // namespace ripple
