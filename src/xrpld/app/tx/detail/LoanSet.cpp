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
#include <xrpld/app/misc/LendingHelpers.h>
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
#include <xrpl/protocol/Protocol.h>
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

    if (auto const ret =
            ripple::detail::preflightCheckSigningKey(counterPartySig, ctx.j))
        return ret;

    if (auto const data = tx[~sfData]; data && !data->empty() &&
        !validDataLength(tx[~sfData], maxDataPayloadLength))
        return temINVALID;
    if (!validNumericRange(tx[~sfOverpaymentFee], maxOverpaymentFee))
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
        paymentInterval < LoanSet::minPaymentInterval)
        return temINVALID;

    else if (auto const gracePeriod =
                 tx[~sfGracePeriod].value_or(LoanSet::defaultGracePeriod);
             gracePeriod > paymentInterval)
        return temINVALID;

    // Copied from preflight2
    if (auto const ret = ripple::detail::preflightCheckSimulateKeys(
            ctx.flags, counterPartySig, ctx.j))
        return *ret;

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
    auto const& tx = ctx.tx;

    if (auto const startDate(tx[sfStartDate]); hasExpired(ctx.view, startDate))
    {
        JLOG(ctx.j.warn()) << "Start date is in the past.";
        return tecEXPIRED;
    }

    auto const account = tx[sfAccount];
    auto const brokerID = tx[sfLoanBrokerID];

    auto const brokerSle = ctx.view.read(keylet::loanbroker(brokerID));
    if (!brokerSle)
    {
        // This can only be hit if there's a counterparty specified, otherwise
        // it'll fail in the signature check
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }
    auto const brokerOwner = brokerSle->at(sfOwner);
    auto const counterparty = tx[~sfCounterparty].value_or(brokerOwner);
    if (account != brokerOwner && counterparty != brokerOwner)
    {
        JLOG(ctx.j.warn()) << "Neither Account nor Counterparty are the owner "
                              "of the LoanBroker.";
        return tecNO_PERMISSION;
    }
    if (account == counterparty)
    {
        JLOG(ctx.j.warn()) << "Account and Counterparty are the same. Can not "
                              "loan money to yourself.";
        return tecNO_PERMISSION;
    }

    auto const borrower = counterparty == brokerOwner ? account : counterparty;
    if (auto const borrowerSle = ctx.view.read(keylet::account(borrower));
        !borrowerSle)
    {
        // It may not be possible to hit this case, because it'll fail the
        // signature check with terNO_ACCOUNT.
        JLOG(ctx.j.warn()) << "Borrower does not exist.";
        return terNO_ACCOUNT;
    }

    auto const brokerPseudo = brokerSle->at(sfAccount);
    auto const vault = ctx.view.read(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vault)
        // Should be impossible
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    Asset const asset = vault->at(sfAsset);

    if (auto const ter = canAddHolding(ctx.view, asset))
        return ter;

    if (isFrozen(ctx.view, brokerOwner, asset) ||
        isFrozen(ctx.view, brokerPseudo, asset))
    {
        JLOG(ctx.j.warn()) << "One of the affected accounts is frozen.";
        return asset.holds<Issue>() ? tecFROZEN : tecLOCKED;
    }
    if (asset.holds<Issue>())
    {
        auto const issue = asset.get<Issue>();
        if (isDeepFrozen(ctx.view, borrower, issue.currency, issue.account))
            return tecFROZEN;
        if (isDeepFrozen(ctx.view, brokerPseudo, issue.currency, issue.account))
            return tecFROZEN;
    }

    auto const principalRequested = tx[sfPrincipalRequested];
    if (auto const assetsAvailable = vault->at(sfAssetsAvailable);
        assetsAvailable < principalRequested)
    {
        JLOG(ctx.j.warn())
            << "Insufficient assets available in the Vault to fund the loan.";
        return tecINSUFFICIENT_FUNDS;
    }
    auto const newDebtTotal = brokerSle->at(sfDebtTotal) + principalRequested;
    if (brokerSle->at(sfDebtMaximum) < newDebtTotal)
    {
        JLOG(ctx.j.warn())
            << "Loan would exceed the maximum debt limit of the LoanBroker.";
        return tecLIMIT_EXCEEDED;
    }
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};
    if (brokerSle->at(sfCoverAvailable) <
        tenthBipsOfValue(newDebtTotal, coverRateMinimum))
    {
        JLOG(ctx.j.warn())
            << "Insufficient first-loss capital to cover the loan.";
        return tecINSUFFICIENT_FUNDS;
    }

    if (auto const originationFee = tx[~sfLoanOriginationFee])
    {
        // Check that the lender will not make an unfair profit on the lending
        // fee if the loan defaults. (Not yet in spec. May not be included.)
        TenthBips32 const coverRateLiquidation{
            brokerSle->at(sfCoverRateLiquidation)};

        auto const maximumOriginationFee = tenthBipsOfValue(
            tenthBipsOfValue(newDebtTotal, coverRateMinimum),
            coverRateLiquidation);

        if (*originationFee > maximumOriginationFee)
        {
            JLOG(ctx.j.warn())
                << "Loan origination fee is too high. The lender will make a "
                   "profit on the lending fee if the loan defaults.";
            return tecINSUFFICIENT_FUNDS;
        }
    }

    return tesSUCCESS;
}

TER
LoanSet::doApply()
{
    auto const& tx = ctx_.tx;
    auto& view = ctx_.view();

    auto const brokerID = tx[sfLoanBrokerID];

    auto const brokerSle = view.peek(keylet::loanbroker(brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const brokerOwner = brokerSle->at(sfOwner);
    auto const brokerOwnerSle = view.peek(keylet::account(brokerOwner));
    if (!brokerOwnerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    auto const vaultSle = view.peek(keylet ::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultPseudo = vaultSle->at(sfAccount);
    Asset const vaultAsset = vaultSle->at(sfAsset);

    auto const counterparty = tx[~sfCounterparty].value_or(brokerOwner);
    auto const borrower = counterparty == brokerOwner ? account_ : counterparty;
    auto const borrowerSle = view.peek(keylet::account(borrower));
    if (!borrowerSle)
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    auto const brokerPseudo = brokerSle->at(sfAccount);
    auto const brokerPseudoSle = view.peek(keylet::account(brokerPseudo));
    if (!brokerPseudoSle)
    {
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }
    auto const principalRequested = tx[sfPrincipalRequested];
    TenthBips32 const interestRate{tx[~sfInterestRate].value_or(0)};
    auto const originationFee = tx[~sfLoanOriginationFee];
    auto const loanAssetsAvailable =
        principalRequested - originationFee.value_or(Number{});

    adjustOwnerCount(view, borrowerSle, 1, j_);
    auto ownerCount = borrowerSle->at(sfOwnerCount);
    if (mPriorBalance < view.fees().accountReserve(ownerCount))
        return tecINSUFFICIENT_RESERVE;

    // Create a holding for the borrower if one does not already exist.

    // Account for the origination fee using two payments
    //
    // 1. Transfer loanAssetsAvailable (principalRequested - originationFee)
    // from vault pseudo-account to LoanBroker pseudo-account.
    if (auto const ter = accountSend(
            view,
            vaultPseudo,
            brokerPseudo,
            STAmount{vaultAsset, loanAssetsAvailable},
            j_,
            WaiveTransferFee::Yes))
        return ter;
    // 2. Transfer originationFee, if any, from vault pseudo-account to
    // LoanBroker owner.
    if (originationFee)
    {
        // Create the holding if it doesn't already exist (necessary for MPTs).
        // The owner may have deleted their MPT / line at some point.
        if (auto const ter = addEmptyHolding(
                view,
                brokerOwner,
                brokerOwnerSle->at(sfBalance).value().xrp(),
                vaultAsset,
                j_);
            ter != tesSUCCESS && ter != tecDUPLICATE)
            // ignore tecDUPLICATE. That means the holding already exists, and
            // is fine here
            return ter;
        if (auto const ter = accountSend(
                view,
                vaultPseudo,
                brokerOwner,
                STAmount{vaultAsset, *originationFee},
                j_,
                WaiveTransferFee::Yes))
            return ter;
    }

    auto const paymentInterval =
        tx[~sfPaymentInterval].value_or(defaultPaymentInterval);
    auto const paymentTotal = tx[~sfPaymentTotal].value_or(defaultPaymentTotal);
    TenthBips32 const managementFeeRate{brokerSle->at(sfManagementFeeRate)};
    // The portion of the loan interest that will go to the vault (total
    // interest minus the management fee)
    auto const loanInterestToVault = LoanInterestOutstandingMinusFee(
        vaultAsset,
        principalRequested,
        interestRate,
        paymentInterval,
        paymentTotal,
        managementFeeRate);
    auto const startDate = tx[sfStartDate];
    auto loanSequence = brokerSle->at(sfLoanSequence);

    // Create the loan
    auto loan = std::make_shared<SLE>(keylet::loan(brokerID, *loanSequence));

    // Prevent copy/paste errors
    auto setLoanField =
        [&loan, &tx](auto const& field, std::uint32_t const defValue = 0) {
            // at() is smart enough to unseat a default field set to the default
            // value
            loan->at(field) = tx[field].value_or(defValue);
        };

    // Set required tx fields and pre-computed fields
    loan->at(sfPrincipalOutstanding) = principalRequested;
    loan->at(sfStartDate) = startDate;
    loan->at(sfPaymentInterval) = paymentInterval;
    loan->at(sfLoanSequence) = loanSequence;
    loan->at(sfLoanBrokerID) = brokerID;
    loan->at(sfBorrower) = borrower;
    // Set all other transaction fields directly from the transaction
    if (tx.isFlag(tfLoanOverpayment))
        loan->setFlag(lsfLoanOverpayment);
    setLoanField(~sfLoanOriginationFee);
    setLoanField(~sfLoanServiceFee);
    setLoanField(~sfLatePaymentFee);
    setLoanField(~sfClosePaymentFee);
    setLoanField(~sfOverpaymentFee);
    setLoanField(~sfInterestRate);
    setLoanField(~sfLateInterestRate);
    setLoanField(~sfCloseInterestRate);
    setLoanField(~sfOverpaymentInterestRate);
    setLoanField(~sfGracePeriod, defaultGracePeriod);
    // Set dynamic fields to their initial values
    loan->at(sfPreviousPaymentDate) = 0;
    loan->at(sfNextPaymentDueDate) = startDate + paymentInterval;
    loan->at(sfPaymentRemaining) = paymentTotal;
    loan->at(sfAssetsAvailable) = loanAssetsAvailable;
    loan->at(sfPrincipalOutstanding) = principalRequested;
    view.insert(loan);

    // Update the balances in the vault
    vaultSle->at(sfAssetsAvailable) -= principalRequested;
    vaultSle->at(sfAssetsTotal) += loanInterestToVault;
    view.update(vaultSle);

    // Update the balances in the loan broker
    brokerSle->at(sfDebtTotal) += principalRequested + loanInterestToVault;
    // The broker's owner count is solely for the number of outstanding loans,
    // and is distinct from the broker's pseudo-account's owner count
    adjustOwnerCount(view, brokerSle, 1, j_);
    loanSequence += 1;
    view.update(brokerSle);

    // Put the loan into the pseudo-account's directory
    if (auto const ter = dirLink(view, brokerPseudo, loan, sfLoanBrokerNode))
        return ter;
    // Borrower is the owner of the loan
    if (auto const ter = dirLink(view, borrower, loan, sfOwnerNode))
        return ter;

    return tesSUCCESS;
}

//------------------------------------------------------------------------------

}  // namespace ripple
