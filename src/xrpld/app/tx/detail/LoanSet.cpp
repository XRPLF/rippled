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

#include <xrpl/protocol/TxFlags.h>

namespace ripple {

bool
LoanSet::checkExtraFeatures(PreflightContext const& ctx)
{
    return checkLendingProtocolDependencies(ctx);
}

std::uint32_t
LoanSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfLoanSetMask;
}

NotTEC
LoanSet::preflight(PreflightContext const& ctx)
{
    auto const& tx = ctx.tx;

    // Special case for Batch inner transactions
    if (tx.isFlag(tfInnerBatchTxn) && ctx.rules.enabled(featureBatch) &&
        !tx.isFieldPresent(sfCounterparty))
    {
        auto const parentBatchId = ctx.parentBatchId.value_or(uint256{0});
        JLOG(ctx.j.debug()) << "BatchTrace[" << parentBatchId << "]: "
                            << "no Counterparty for inner LoanSet transaction.";
        return temBAD_SIGNER;
    }

    // These extra hoops are because STObjects cannot be Proxy'd from STObject.
    auto const counterPartySig = [&tx]() -> std::optional<STObject const> {
        if (tx.isFieldPresent(sfCounterpartySignature))
            return tx.getFieldObject(sfCounterpartySignature);
        return std::nullopt;
    }();
    if (!tx.isFlag(tfInnerBatchTxn) && !counterPartySig)
    {
        JLOG(ctx.j.warn())
            << "LoanSet transaction must have a CounterpartySignature.";
        return temBAD_SIGNER;
    }

    if (counterPartySig)
    {
        if (auto const ret = ripple::detail::preflightCheckSigningKey(
                *counterPartySig, ctx.j))
            return ret;
    }

    if (auto const data = tx[~sfData]; data && !data->empty() &&
        !validDataLength(tx[~sfData], maxDataPayloadLength))
        return temINVALID;
    for (auto const& field :
         {&sfLoanOriginationFee,
          &sfLoanServiceFee,
          &sfLatePaymentFee,
          &sfClosePaymentFee,
          &sfPrincipalRequested})
    {
        if (!validNumericMinimum(tx[~*field]))
            return temINVALID;
    }
    if (!validNumericRange(tx[~sfInterestRate], maxInterestRate))
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
        paymentTotal && *paymentTotal <= 0)
        return temINVALID;

    if (auto const paymentInterval = tx[~sfPaymentInterval];
        !validNumericMinimum(paymentInterval, LoanSet::minPaymentInterval))
        return temINVALID;

    else if (!validNumericRange(
                 tx[~sfGracePeriod],
                 paymentInterval.value_or(LoanSet::defaultPaymentInterval)))
        return temINVALID;

    // Copied from preflight2
    if (counterPartySig)
    {
        if (auto const ret = ripple::detail::preflightCheckSimulateKeys(
                ctx.flags, *counterPartySig, ctx.j))
            return *ret;
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

    // Counterparty signature is optional. Presence is checked in preflight.
    if (!ctx.tx.isFieldPresent(sfCounterpartySignature))
        return tesSUCCESS;
    auto const counterSig = ctx.tx.getFieldObject(sfCounterpartySignature);
    return Transactor::checkSign(
        ctx.view,
        ctx.flags,
        ctx.parentBatchId,
        *counterSigner,
        counterSig,
        ctx.j);
}

XRPAmount
LoanSet::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    auto const normalCost = Transactor::calculateBaseFee(view, tx);

    // Compute the additional cost of each signature in the
    // CounterpartySignature, whether a single signature or a multisignature
    XRPAmount const baseFee = view.fees().base;

    // Counterparty signature is optional, but getFieldObject will return an
    // empty object if it's not present.
    auto const counterSig = tx.getFieldObject(sfCounterpartySignature);
    // Each signer adds one more baseFee to the minimum required fee
    // for the transaction. Note that unlike the base class, the single signer
    // is counted if present. It will only be absent in a batch inner
    // transaction.
    std::size_t const signerCount = tx.isFieldPresent(sfSigners)
        ? tx.getFieldArray(sfSigners).size()
        : (counterSig.isFieldPresent(sfTxnSignature) ? 1 : 0);

    return normalCost + (signerCount * baseFee);
}

TER
LoanSet::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

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

    auto const borrower = counterparty == brokerOwner ? account : counterparty;
    if (auto const borrowerSle = ctx.view.read(keylet::account(borrower));
        !borrowerSle)
    {
        // It may not be possible to hit this case, because it'll fail the
        // signature check with terNO_ACCOUNT.
        JLOG(ctx.j.warn()) << "Borrower does not exist.";
        return terNO_ACCOUNT;
    }

    auto const vault = ctx.view.read(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vault)
        // Should be impossible
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    Asset const asset = vault->at(sfAsset);
    auto const vaultPseudo = vault->at(sfAccount);

    if (auto const ter = canAddHolding(ctx.view, asset))
        return ter;

    // vaultPseudo is going to send funds, so it can't be frozen.
    if (auto const ret = checkFrozen(ctx.view, vaultPseudo, asset))
    {
        JLOG(ctx.j.warn()) << "Vault pseudo-account is frozen.";
        return ret;
    }
    // borrower is eventually going to have to pay back the loan, so it can't be
    // frozen now. It is also going to receive funds, so it can't be deep
    // frozen, but being frozen is a prerequisite for being deep frozen, so
    // checking the one is sufficient.
    if (auto const ret = checkFrozen(ctx.view, borrower, asset))
    {
        JLOG(ctx.j.warn()) << "Borrower account is frozen.";
        return ret;
    }
    // brokerOwner is going to receive funds if there's an origination fee, so
    // it can't be deep frozen
    if (auto const ret = checkDeepFrozen(ctx.view, brokerOwner, asset))
    {
        JLOG(ctx.j.warn()) << "Broker owner account is frozen.";
        return ret;
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

    if (auto const assetsAvailable = vaultSle->at(sfAssetsAvailable);
        assetsAvailable < principalRequested)
    {
        JLOG(j_.warn())
            << "Insufficient assets available in the Vault to fund the loan.";
        return tecINSUFFICIENT_FUNDS;
    }

    TenthBips32 const interestRate{tx[~sfInterestRate].value_or(0)};

    auto const paymentInterval =
        tx[~sfPaymentInterval].value_or(defaultPaymentInterval);
    auto const paymentTotal = tx[~sfPaymentTotal].value_or(defaultPaymentTotal);

    auto const properties = computeLoanProperties(
        vaultAsset,
        principalRequested,
        principalRequested,
        interestRate,
        paymentInterval,
        paymentTotal,
        TenthBips32{brokerSle->at(sfManagementFeeRate)});

    if (properties.firstPaymentPrincipal <= 0)
    {
        // Check that some reference principal is paid each period. Since the
        // first payment pays the least principal, if it's good, they'll all be
        // good. Note that the outstanding principal is rounded, and may not
        // change right away.
        JLOG(j_.warn()) << "Loan is unable to pay principal.";
        return tecLIMIT_EXCEEDED;
    }
    // Check that the other computed values are valid
    if (properties.interestOwedToVault < 0 ||
        properties.totalValueOutstanding <= 0 ||
        properties.periodicPayment <= 0)
    {
        // LCOV_EXCL_START
        JLOG(j_.warn())
            << "Computed loan properties are invalid. Does not compute.";
        return tecINTERNAL;
        // LCOV_EXCL_STOP
    }

    // Check that relevant values won't lose precision
    {
        static std::map<std::string, OptionaledField<STNumber>> const
            valueFields{
                {"Principal Requested", ~sfPrincipalRequested},
                {"Origination fee", ~sfLoanOriginationFee},
                {"Service fee", ~sfLoanServiceFee},
                {"Late Payment fee", ~sfLatePaymentFee},
                {"Close Payment fee", ~sfClosePaymentFee}
                // Overpayment fee is really a rate. Don't include it.
            };
        for (auto const& [name, field] : valueFields)
        {
            if (auto const value = tx[field];
                value && !isRounded(vaultAsset, *value, properties.loanScale))
            {
                JLOG(j_.warn()) << name << " has too much precision.";
                return tecPRECISION_LOSS;
            }
        }
    }
    auto const originationFee = tx[~sfLoanOriginationFee].value_or(Number{});

    auto const loanAssetsToBorrower = principalRequested - originationFee;

    auto const newDebtDelta =
        principalRequested + properties.interestOwedToVault;
    auto const newDebtTotal = brokerSle->at(sfDebtTotal) + newDebtDelta;
    if (auto const debtMaximum = brokerSle->at(sfDebtMaximum);
        debtMaximum != 0 && debtMaximum < newDebtTotal)
    {
        JLOG(j_.warn())
            << "Loan would exceed the maximum debt limit of the LoanBroker.";
        return tecLIMIT_EXCEEDED;
    }
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};
    if (brokerSle->at(sfCoverAvailable) <
        tenthBipsOfValue(newDebtTotal, coverRateMinimum))
    {
        JLOG(j_.warn()) << "Insufficient first-loss capital to cover the loan.";
        return tecINSUFFICIENT_FUNDS;
    }

    adjustOwnerCount(view, borrowerSle, 1, j_);
    {
        auto ownerCount = borrowerSle->at(sfOwnerCount);
        if (mPriorBalance < view.fees().accountReserve(ownerCount))
            return tecINSUFFICIENT_RESERVE;
    }

    // Account for the origination fee using two payments
    //
    // 1. Transfer loanAssetsAvailable (principalRequested - originationFee)
    // from vault pseudo-account to the borrower.
    // Create a holding for the borrower if one does not already exist.
    if (auto const ter = addEmptyHolding(
            view,
            borrower,
            borrowerSle->at(sfBalance).value().xrp(),
            vaultAsset,
            j_);
        ter && ter != tecDUPLICATE)
        // ignore tecDUPLICATE. That means the holding already exists, and
        // is fine here
        return ter;

    if (auto const ter = accountSend(
            view,
            vaultPseudo,
            borrower,
            STAmount{vaultAsset, loanAssetsToBorrower},
            j_,
            WaiveTransferFee::Yes))
        return ter;
    // 2. Transfer originationFee, if any, from vault pseudo-account to
    // LoanBroker owner.
    if (originationFee != Number{})
    {
        // Create the holding if it doesn't already exist (necessary for MPTs).
        // The owner may have deleted their MPT / line at some point.
        if (auto const ter = addEmptyHolding(
                view,
                brokerOwner,
                brokerOwnerSle->at(sfBalance).value().xrp(),
                vaultAsset,
                j_);
            !isTesSuccess(ter) && ter != tecDUPLICATE)
            // ignore tecDUPLICATE. That means the holding already exists, and
            // is fine here
            return ter;
        if (auto const ter = accountSend(
                view,
                vaultPseudo,
                brokerOwner,
                STAmount{vaultAsset, originationFee},
                j_,
                WaiveTransferFee::Yes))
            return ter;
    }

    // The portion of the loan interest that will go to the vault (total
    // interest minus the management fee)
    auto const startDate = view.info().closeTime.time_since_epoch().count();
    auto loanSequenceProxy = brokerSle->at(sfLoanSequence);

    // Create the loan
    auto loan =
        std::make_shared<SLE>(keylet::loan(brokerID, *loanSequenceProxy));

    // Prevent copy/paste errors
    auto setLoanField =
        [&loan, &tx](auto const& field, std::uint32_t const defValue = 0) {
            // at() is smart enough to unseat a default field set to the default
            // value
            loan->at(field) = tx[field].value_or(defValue);
        };

    // Set required and fixed tx fields
    loan->at(sfLoanScale) = principalRequested.exponent();
    loan->at(sfStartDate) = startDate;
    loan->at(sfPaymentInterval) = paymentInterval;
    loan->at(sfLoanSequence) = *loanSequenceProxy;
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
    // Set dynamic / computed fields to their initial values
    loan->at(sfPrincipalOutstanding) = principalRequested;
    loan->at(sfReferencePrincipal) = principalRequested;
    loan->at(sfPeriodicPayment) = properties.periodicPayment;
    loan->at(sfTotalValueOutstanding) = properties.totalValueOutstanding;
    loan->at(sfInterestOwed) = properties.interestOwedToVault;
    loan->at(sfPreviousPaymentDate) = 0;
    loan->at(sfNextPaymentDueDate) = startDate + paymentInterval;
    loan->at(sfPaymentRemaining) = paymentTotal;
    view.insert(loan);

    // Update the balances in the vault
    vaultSle->at(sfAssetsAvailable) -= principalRequested;
    vaultSle->at(sfAssetsTotal) += properties.interestOwedToVault;
    view.update(vaultSle);

    // Update the balances in the loan broker
    brokerSle->at(sfDebtTotal) += newDebtDelta;
    // The broker's owner count is solely for the number of outstanding loans,
    // and is distinct from the broker's pseudo-account's owner count
    adjustOwnerCount(view, brokerSle, 1, j_);
    loanSequenceProxy += 1;
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
