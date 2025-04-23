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

#include <xrpld/app/tx/detail/LoanBrokerSet.h>
#include <xrpld/app/tx/detail/SignerEntries.h>
#include <xrpld/app/tx/detail/VaultCreate.h>
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
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

bool
lendingProtocolEnabled(PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureLendingProtocol) &&
        VaultCreate::isEnabled(ctx);
}

bool
LoanBrokerSet::isEnabled(PreflightContext const& ctx)
{
    return lendingProtocolEnabled(ctx);
}

std::uint32_t
LoanBrokerSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfUniversalMask;
}

NotTEC
LoanBrokerSet::doPreflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    if (auto const data = tx[~sfData]; data && !data->empty() &&
        !validDataLength(tx[~sfData], maxDataPayloadLength))
        return temINVALID;
    if (!validNumericRange(tx[~sfManagementFeeRate], maxManagementFeeRate))
        return temINVALID;
    if (!validNumericRange(tx[~sfCoverRateMinimum], maxCoverRate))
        return temINVALID;
    if (!validNumericRange(tx[~sfCoverRateLiquidation], maxCoverRate))
        return temINVALID;
    if (!validNumericRange(
            tx[~sfDebtMaximum], Number(maxMPTokenAmount), Number(0)))
        return temINVALID;

    if (tx.isFieldPresent(sfLoanBrokerID))
    {
        // Fixed fields can not be specified if we're modifying an existing
        // LoanBroker Object
        if (tx.isFieldPresent(sfManagementFeeRate) ||
            tx.isFieldPresent(sfCoverRateMinimum) ||
            tx.isFieldPresent(sfCoverRateLiquidation))
            return temINVALID;
    }

    return tesSUCCESS;
}

XRPAmount
LoanBrokerSet::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // One reserve increment is typically much greater than one base fee.
    if (!tx.isFieldPresent(sfLoanBrokerID))
        return calculateOwnerReserveFee(view, tx);
    return Transactor::calculateBaseFee(view, tx);
}

TER
LoanBrokerSet::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    if (auto const brokerID = tx[~sfLoanBrokerID])
    {
        auto const sleBroker = ctx.view.read(keylet::loanbroker(*brokerID));
        if (!sleBroker)
        {
            JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
            return tecNO_ENTRY;
        }
        if (tx[sfVaultID] != sleBroker->at(sfVaultID))
        {
            JLOG(ctx.j.warn())
                << "Can not change VaultID on an existing LoanBroker.";
            return tecNO_PERMISSION;
        }
        if (account != sleBroker->at(sfOwner))
        {
            JLOG(ctx.j.warn()) << "Account is not the owner of the LoanBroker.";
            return tecNO_PERMISSION;
        }
    }
    else
    {
        auto const vaultID = tx[sfVaultID];
        auto const sleVault = ctx.view.read(keylet::vault(vaultID));
        if (!sleVault)
        {
            JLOG(ctx.j.warn()) << "Vault does not exist.";
            return tecNO_ENTRY;
        }
        if (account != sleVault->at(sfOwner))
        {
            JLOG(ctx.j.warn()) << "Account is not the owner of the Vault.";
            return tecNO_PERMISSION;
        }
    }
    return tesSUCCESS;
}

TER
LoanBrokerSet::doApply()
{
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

    if (auto const brokerID = tx[~sfLoanBrokerID])
    {
        // Modify an existing LoanBroker
        auto broker = view.peek(keylet::loanbroker(*brokerID));
        if (!broker)
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE

        if (auto const data = tx[~sfData])
            broker->at(sfData) = *data;
        if (auto const debtMax = tx[~sfDebtMaximum])
            broker->at(sfDebtMaximum) = *debtMax;

        view.update(broker);
    }
    else
    {
        // Create a new LoanBroker pointing back to the given Vault
        auto const vaultID = tx[sfVaultID];
        auto const sleVault = view.read(keylet::vault(vaultID));
        auto const vaultPseudoID = sleVault->at(sfAccount);
        auto const sequence = tx.getSeqValue();

        auto owner = view.peek(keylet::account(account_));
        if (!owner)
            return tefBAD_LEDGER;  // LCOV_EXCL_LINE
        auto broker =
            std::make_shared<SLE>(keylet::loanbroker(account_, sequence));

        if (auto const ter = dirLink(view, account_, broker))
            return ter;
        if (auto const ter = dirLink(view, vaultPseudoID, broker, sfVaultNode))
            return ter;

        /* We're already charging a higher fee, so we probably don't want to
        also charge a reserve.
        *
        adjustOwnerCount(view, owner, 1, j_);
        auto ownerCount = owner->at(sfOwnerCount);
        if (mPriorBalance < view.fees().accountReserve(ownerCount))
            return tecINSUFFICIENT_RESERVE;
            */

        auto maybePseudo =
            createPseudoAccount(view, broker->key(), sfLoanBrokerID);
        if (!maybePseudo)
            return maybePseudo.error();
        auto& pseudo = *maybePseudo;
        auto pseudoId = pseudo->at(sfAccount);

        if (auto ter = addEmptyHolding(
                view, pseudoId, mPriorBalance, sleVault->at(sfAsset), j_))
            return ter;

        // Initialize data fields:
        broker->at(sfSequence) = sequence;
        broker->at(sfVaultID) = vaultID;
        broker->at(sfOwner) = account_;
        broker->at(sfAccount) = pseudoId;
        if (auto const data = tx[~sfData])
            broker->at(sfData) = *data;
        if (auto const rate = tx[~sfManagementFeeRate])
            broker->at(sfManagementFeeRate) = *rate;
        if (auto const debtMax = tx[~sfDebtMaximum])
            broker->at(sfDebtMaximum) = *debtMax;
        if (auto const coverMin = tx[~sfCoverRateMinimum])
            broker->at(sfCoverRateMinimum) = *coverMin;
        if (auto const coverLiq = tx[~sfCoverRateLiquidation])
            broker->at(sfCoverRateLiquidation) = *coverLiq;

        view.insert(broker);
    }

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
