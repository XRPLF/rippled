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

#include <xrpld/app/tx/detail/LoanSet.h>
//
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
LoanSet::isEnabled(PreflightContext const& ctx)
{
    return lendingProtocolEnabled(ctx);
}

std::uint32_t
LoanSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfLoanSetMask;
}

NotTEC
LoanSet::doPreflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;
    auto const counterPartySig = ctx.tx.getFieldObject(sfCounterpartySignature);

    // Copied from preflight1
    // TODO: Refactor into a helper function?
    if (auto const spk = counterPartySig.getFieldVL(sfSigningPubKey);
        !spk.empty() && !publicKeyType(makeSlice(spk)))
    {
        JLOG(ctx.j.debug()) << "preflight1: invalid signing key";
        return temBAD_SIGNATURE;
    }

    if (auto const data = tx[~sfData]; data && !data->empty() &&
        !validDataLength(tx[~sfData], maxDataPayloadLength))
        return temINVALID;
    if (!validNumericRange(tx[~sfLateInterestRate], maxLateInterestRate))
        return temINVALID;
    if (!validNumericRange(tx[~sfCloseInterestRate], maxCloseInterestRate))
        return temINVALID;
    if (!validNumericRange(
            tx[~sfOverpaymentInterestRate], maxOverpaymentInterestRate))
        return temINVALID;

    if (auto const paymentTotal = tx[~sfPaymentTotal];
        paymentTotal && *paymentTotal == 0)
        return temINVALID;
    if (auto const paymentInterval =
            tx[~sfPaymentInterval].value_or(LoanSet::defaultPaymentInterval);
        paymentInterval < LoanSet::defaultPaymentInterval)
        return temINVALID;
    else if (auto const gracePeriod =
                 tx[~sfGracePeriod].value_or(LoanSet::defaultGracePeriod);
             gracePeriod > paymentInterval)
        return temINVALID;

    // Copied from preflight2
    // TODO: Refactor into a helper function?
    if (ctx.flags & tapDRY_RUN)  // simulation
    {
        if (!ctx.tx.getSignature(counterPartySig).empty())
        {
            // NOTE: This code should never be hit because it's checked in the
            // `simulate` RPC
            return temINVALID;  // LCOV_EXCL_LINE
        }

        if (!counterPartySig.isFieldPresent(sfSigners))
        {
            // no signers, no signature - a valid simulation
            return tesSUCCESS;
        }

        for (auto const& signer : counterPartySig.getFieldArray(sfSigners))
        {
            if (signer.isFieldPresent(sfTxnSignature) &&
                !signer[sfTxnSignature].empty())
            {
                // NOTE: This code should never be hit because it's
                // checked in the `simulate` RPC
                return temINVALID;  // LCOV_EXCL_LINE
            }
        }
        return tesSUCCESS;
    }

    return tesSUCCESS;
}

NotTEC
LoanSet::checkSign(PreclaimContext const& ctx)
{
    if (auto ret = Transactor::checkSign(ctx))
        return ret;

    // Counter signer is optional. If it's not specified, it's assumed to be
    // `LoanBroker.Owner`. Note that we have not checked whether the
    // loanbroker exists at this point.
    auto const counterSigner = [&]() -> std::optional<AccountID> {
        if (auto const c = ctx.tx.at(~sfCounterparty))
            return c;

        if (auto const broker =
                ctx.view.read(keylet::loanbroker(ctx.tx[sfLoanBrokerID])))
            return broker->at(sfOwner);
        return std::nullopt;
    }();
    if (!counterSigner)
        return temBAD_SIGNER;
    // Counterparty signature is required
    auto const counterSig = ctx.tx.getFieldObject(sfCounterpartySignature);
    return Transactor::checkSign(ctx, *counterSigner, counterSig);
}
}

XRPAmount
LoanSet::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const normalCost = Transactor::calculateBaseFee(view, tx);

    // Compute the additional cost of each signature in the
    // CounterpartySignature, whether a single signature or a multisignature
    XRPAmount const baseFee = view.fees().base;

    auto const counterSig = tx.getFieldObject(sfCounterpartySignature);
    // Each signer adds one more baseFee to the minimum required fee
    // for the transaction. Note that unlike the base class, if there are no
    // signers, 1 extra signature is still counted for the single signer.
    std::size_t const signerCount =
        tx.isFieldPresent(sfSigners) ? tx.getFieldArray(sfSigners).size() : 1;

    return normalCost + (signerCount * baseFee);
}

TER
LoanSet::preclaim(PreclaimContext const& ctx)
{
    return temDISABLED;

    auto const& tx = ctx.tx;

    auto const account = tx[sfAccount];
    if (auto const ID = tx[~sfLoanID])
    {
        auto const sle = ctx.view.read(keylet::loan(*ID));
        if (!sle)
        {
            JLOG(ctx.j.warn()) << "Loan does not exist.";
            return tecNO_ENTRY;
        }
        if (tx[sfVaultID] != sle->at(sfVaultID))
        {
            JLOG(ctx.j.warn()) << "Can not change VaultID on an existing Loan.";
            return tecNO_PERMISSION;
        }
        if (account != sle->at(sfOwner))
        {
            JLOG(ctx.j.warn()) << "Account is not the owner of the Loan.";
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
LoanSet::doApply()
{
    return temDISABLED;

    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

#if 0
    if (auto const ID = tx[~sfLoanID])
    {
        // Modify an existing Loan
        auto loan = view.peek(keylet::loan(*ID));

        if (auto const data = tx[~sfData])
            loan->at(sfData) = *data;
        if (auto const debtMax = tx[~sfDebtMaximum])
            loan->at(sfDebtMaximum) = *debtMax;

        view.update();
    }
    else
    {
        // Create a new Loan pointing back to the given Vault
        auto const vaultID = tx[sfVaultID];
        auto const sleVault = view.read(keylet::vault(vaultID));
        auto const vaultPseudoID = sleVault->at(sfAccount);
        auto const sequence = tx.getSeqValue();

        auto owner = view.peek(keylet::account(account_));
        auto loan = std::make_shared<SLE>(keylet::loan(account_, sequence));

        if (auto const ter = dirLink(view, account_, ))
            return ter;
        if (auto const ter = dirLink(view, vaultPseudoID, , sfVaultNode))
            return ter;

        adjustOwnerCount(view, owner, 1, j_);
        auto ownerCount = owner->at(sfOwnerCount);
        if (mPriorBalance < view.fees().accountReserve(ownerCount))
            return tecINSUFFICIENT_RESERVE;

        auto maybePseudo =
            createPseudoAccount(view, loan->key(), PseudoAccountOwnerType::Loan);
        if (!maybePseudo)
            return maybePseudo.error();
        auto& pseudo = *maybePseudo;
        auto pseudoId = pseudo->at(sfAccount);

        if (auto ter = addEmptyHolding(
                view, pseudoId, mPriorBalance, sleVault->at(sfAsset), j_))
            return ter;

        // Initialize data fields:
        loan->at(sfSequence) = sequence;
        loan->at(sfVaultID) = vaultID;
        loan->at(sfOwner) = account_;
        loan->at(sfAccount) = pseudoId;
        if (auto const data = tx[~sfData])
            loan->at(sfData) = *data;
        if (auto const rate = tx[~sfManagementFeeRate])
            loan->at(sfManagementFeeRate) = *rate;
        if (auto const debtMax = tx[~sfDebtMaximum])
            loan->at(sfDebtMaximum) = *debtMax;
        if (auto const coverMin = tx[~sfCoverRateMinimum])
            loan->at(sfCoverRateMinimum) = *coverMin;
        if (auto const coverLiq = tx[~sfCoverRateLiquidation])
            loan->at(sfCoverRateLiquidation) = *coverLiq;

        view.insert(loan);
    }
#endif

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
