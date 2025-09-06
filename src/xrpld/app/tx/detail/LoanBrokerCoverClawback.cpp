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

#include <xrpld/app/tx/detail/LoanBrokerCoverClawback.h>
//
#include <xrpld/app/misc/LendingHelpers.h>
#include <xrpld/app/tx/detail/Payment.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

bool
LoanBrokerCoverClawback::isEnabled(PreflightContext const& ctx)
{
    return lendingProtocolEnabled(ctx);
}

NotTEC
LoanBrokerCoverClawback::preflight(PreflightContext const& ctx)
{
    auto const brokerID = ctx.tx[~sfLoanBrokerID];
    auto const amount = ctx.tx[~sfAmount];

    if (!brokerID && !amount)
        return temINVALID;

    if (brokerID && *brokerID == beast::zero)
        return temINVALID;

    if (amount)
    {
        // XRP has no counterparty, and thus nobody can claw it back
        if (amount->native())
            return temBAD_AMOUNT;

        // Zero is OK, and indicates "take it all" (down to the minimum cover)
        if (*amount < beast::zero)
            return temBAD_AMOUNT;

        // This should be redundant
        if (!isLegalNet(*amount))
            return temBAD_AMOUNT;  // LCOV_EXCL_LINE

        if (!brokerID)
        {
            if (amount->holds<MPTIssue>())
                return temINVALID;

            auto const account = ctx.tx[sfAccount];
            // Since we don't have a LoanBrokerID, holder _should_ be the loan
            // broker's pseudo-account, but we don't know yet whether it is, so
            // use a generic placeholder name.
            auto const holder = amount->getIssuer();
            if (holder == account || holder == beast::zero)
                return temINVALID;
        }
    }

    return tesSUCCESS;
}

Expected<uint256, TER>
determineBrokerID(ReadView const& view, STTx const& tx)
{
    if (auto const brokerID = tx[~sfLoanBrokerID])
        return *brokerID;

    auto const dstAmount = tx[~sfAmount];
    if (!dstAmount || !dstAmount->holds<Issue>())
        return Unexpected{tecINTERNAL};

    // Since we don't have a LoanBrokerID, holder _should_ be the loan broker's
    // pseudo-account, but we don't know yet whether it is, so use a generic
    // placeholder name.
    auto const holder = dstAmount->getIssuer();
    auto const sle = view.read(keylet::account(holder));
    if (!sle)
        return Unexpected{tecNO_ENTRY};

    if (auto const brokerID = sle->at(~sfLoanBrokerID))
        return *brokerID;

    // Or tecWRONG_ASSET?
    return Unexpected{tecOBJECT_NOT_FOUND};
}

Expected<Asset, TER>
determineAsset(
    ReadView const& view,
    AccountID const& account,
    AccountID const& brokerPseudoAccountID,
    STAmount const& amount)
{
    if (amount.holds<MPTIssue>())
        return amount.asset();

    // An IOU has an issue, which could be either end of the trust line.
    // This check only applies to IOUs
    auto const holder = amount.getIssuer();

    // holder can be the submitting account (the issuer of the asset) if a
    // LoanBrokerID was provided in the transaction.
    if (holder == account)
    {
        return amount.asset();
    }
    else if (holder == brokerPseudoAccountID)
    {
        // We want the asset to match the vault asset, so use the account as the
        // issuer
        return Issue{amount.getCurrency(), account};
    }
    else
        return Unexpected(tecWRONG_ASSET);
}

Expected<STAmount, TER>
determineClawAmount(
    SLE const& sleBroker,
    Asset const& vaultAsset,
    std::optional<STAmount> const& amount)
{
    auto const maxClawAmount = sleBroker[sfCoverAvailable] -
        tenthBipsOfValue(sleBroker[sfDebtTotal],
                         TenthBips32(sleBroker[sfCoverRateMinimum]));
    if (maxClawAmount <= beast::zero)
        return Unexpected(tecINSUFFICIENT_FUNDS);

    // Use the vaultAsset here, because it will be the right type in all
    // circumstances. The amount may be an IOU indicating the pseudo-account's
    // asset, which is correct, but not what is needed here.
    if (!amount || *amount == beast::zero)
        return STAmount{vaultAsset, maxClawAmount};
    Number const magnitude{*amount};
    if (magnitude > maxClawAmount)
        return STAmount{vaultAsset, maxClawAmount};
    return STAmount{vaultAsset, magnitude};
}

template <ValidIssueType T>
static TER
preclaimHelper(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    STAmount const& clawAmount);

template <>
TER
preclaimHelper<Issue>(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    STAmount const& clawAmount)
{
    // If AllowTrustLineClawback is not set or NoFreeze is set, return no
    // permission
    if (!(sleIssuer.isFlag(lsfAllowTrustLineClawback)) ||
        (sleIssuer.isFlag(lsfNoFreeze)))
        return tecNO_PERMISSION;

    return tesSUCCESS;
}

template <>
TER
preclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    STAmount const& clawAmount)
{
    auto const issuanceKey =
        keylet::mptIssuance(clawAmount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanClawback) ||
        !sleIssuance->isFlag(lsfMPTCanLock))
        return tecNO_PERMISSION;

    // With all the checking already done, this should be impossible
    if (sleIssuance->at(sfIssuer) != sleIssuer[sfAccount])
        return tecINTERNAL;  // LCOV_EXCL_LINE

    return tesSUCCESS;
}

TER
LoanBrokerCoverClawback::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    auto const findBrokerID = determineBrokerID(ctx.view, tx);
    if (!findBrokerID)
        return findBrokerID.error();
    auto const brokerID = *findBrokerID;
    auto const amount = tx[~sfAmount];

    auto const sleBroker = ctx.view.read(keylet::loanbroker(brokerID));
    if (!sleBroker)
    {
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }

    auto const brokerPseudoAccountID = sleBroker->at(sfAccount);

    auto const vault = ctx.view.read(keylet::vault(sleBroker->at(sfVaultID)));
    if (!vault)
        return tecINTERNAL;

    auto const vaultAsset = vault->at(sfAsset);

    if (vaultAsset.native())
    {
        JLOG(ctx.j.warn()) << "Cannot clawback native asset.";
        return tecNO_PERMISSION;
    }

    // Only the issuer of the vault asset can claw it back from the broker's
    // cover funds.
    if (vaultAsset.getIssuer() != account)
    {
        JLOG(ctx.j.warn()) << "Account is not the issuer of the vault asset.";
        return tecNO_PERMISSION;
    }

    if (amount)
    {
        auto const findAsset =
            determineAsset(ctx.view, account, brokerPseudoAccountID, *amount);
        if (!findAsset)
            return findAsset.error();
        auto const txAsset = *findAsset;
        if (txAsset != vaultAsset)
        {
            JLOG(ctx.j.warn()) << "Account is the correct issuer, but trying "
                                  "to clawback the wrong asset from LoanBroker";
            return tecWRONG_ASSET;
        }
    }

    auto const findClawAmount =
        determineClawAmount(*sleBroker, vaultAsset, amount);
    if (!findClawAmount)
    {
        JLOG(ctx.j.warn()) << "LoanBroker cover is already at minimum.";
        return findClawAmount.error();
    }
    STAmount const clawAmount = *findClawAmount;

    // Explicitly check the balance of the trust line / MPT to make sure the
    // balance is actually there. It should always match `stCoverAvailable`, so
    // if there isn't, this is an internal error.
    if (accountHolds(
            ctx.view,
            brokerPseudoAccountID,
            vaultAsset,
            fhIGNORE_FREEZE,
            ahIGNORE_AUTH,
            ctx.j) < clawAmount)
        return tecINTERNAL;  // tecINSUFFICIENT_FUNDS; LCOV_EXCL_LINE

    // Check if the vault asset issuer has the correct flags
    auto const sleIssuer =
        ctx.view.read(keylet::account(vaultAsset.getIssuer()));
    return std::visit(
        [&]<typename T>(T const&) {
            return preclaimHelper<T>(ctx, *sleIssuer, clawAmount);
        },
        vaultAsset.value());
}

TER
LoanBrokerCoverClawback::doApply()
{
    auto const& tx = ctx_.tx;
    auto const account = tx[sfAccount];
    auto const findBrokerID = determineBrokerID(view(), tx);
    if (!findBrokerID)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    auto const brokerID = *findBrokerID;
    auto const amount = tx[~sfAmount];

    auto sleBroker = view().peek(keylet::loanbroker(brokerID));
    if (!sleBroker)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const brokerPseudoID = *sleBroker->at(sfAccount);

    auto const vault = view().read(keylet::vault(sleBroker->at(sfVaultID)));
    if (!vault)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const vaultAsset = vault->at(sfAsset);

    auto const findClawAmount =
        determineClawAmount(*sleBroker, vaultAsset, amount);
    if (!findClawAmount)
        return tecINTERNAL;  // LCOV_EXCL_LINE
    STAmount const clawAmount = *findClawAmount;
    // Just for paranoia's sake
    if (clawAmount.native())
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Decrease the LoanBroker's CoverAvailable by Amount
    sleBroker->at(sfCoverAvailable) -= clawAmount;
    view().update(sleBroker);

    // Transfer assets from pseudo-account to depositor.
    return accountSend(
        view(), brokerPseudoID, account, clawAmount, j_, WaiveTransferFee::Yes);
}

//------------------------------------------------------------------------------

}  // namespace ripple
