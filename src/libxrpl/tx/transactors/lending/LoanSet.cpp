#include <xrpl/tx/transactors/lending/LoanSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTakesAsset.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace xrpl {

namespace {

/**
 * The borrower and counterparty accounts resolved for a LoanSet.
 */
struct Participants
{
    AccountID borrower;
    AccountID counterparty;
};

/**
 * Holds the values validated and computed by doApply() that the flow
 * functions need to create the loan and update the ledger.
 *
 * The ledger entries themselves are fetched (and their existence verified)
 * by the flow functions that mutate them; the derived borrower /
 * counterparty account IDs and the pure computed scalars are carried here so
 * they are resolved once, in doApply(), rather than in each flow function.
 */
struct LoanPlan
{
    uint256 brokerID;
    AccountID borrower;
    AccountID counterparty;
    Number principalRequested;
    Number originationFee;
    Number interestDue;
    LoanProperties properties;
    std::uint32_t paymentInterval{};
    std::uint32_t paymentTotal{};
};

/**
 * Holds the LoanBroker entry and the validated / computed scalars produced
 * by setupLoan(): everything doApply() needs to resolve the participants and
 * assemble the LoanPlan.
 */
struct LoanSetup
{
    uint256 brokerID;
    std::shared_ptr<SLE> brokerSle;
    Number principalRequested;
    Number originationFee;
    Number interestDue;
    LoanProperties properties;
    std::uint32_t paymentInterval;
    std::uint32_t paymentTotal;
};

std::uint32_t
currentLedgerCloseTime(ReadView const& view)
{
    return view.header().closeTime.time_since_epoch().count();
}

bool
isTwoStepFlowEnabled(Rules const& rules)
{
    return rules.enabled(featureLendingProtocolV1_1);
}

/**
 * Determines which LoanFlow a LoanSet transaction is requesting from its
 * fields. The two-step flow is only available when the corresponding
 * amendment is enabled; when it is not, transactions carrying two-step
 * fields are reported as Invalid.
 */
LoanFlow
getLoanFlow(STTx const& tx, bool twoStepFlowEnabled)
{
    bool const isBatch = tx.isFlag(tfInnerBatchTxn);
    bool const hasCounterparty = tx.isFieldPresent(sfCounterparty);
    bool const hasCounterpartySignature = tx.isFieldPresent(sfCounterpartySignature);
    bool const hasBorrower = tx.isFieldPresent(sfBorrower);
    bool const hasStartDate = tx.isFieldPresent(sfStartDate);
    bool const hasBorrowerOrStartDate = hasBorrower || hasStartDate;

    if (twoStepFlowEnabled && hasBorrower && hasStartDate && !hasCounterparty &&
        !hasCounterpartySignature)
        return LoanFlow::TwoStep;
    if ((hasCounterpartySignature || isBatch) && !hasBorrowerOrStartDate)
        return LoanFlow::OneStep;
    return LoanFlow::Invalid;
}

std::uint32_t
getStartDate(ReadView const& view, STTx const& tx)
{
    if (getLoanFlow(tx, isTwoStepFlowEnabled(view.rules())) == LoanFlow::TwoStep)
    {
        return tx[sfStartDate];
    }
    return currentLedgerCloseTime(view);
}

/**
 * Resolves the borrower and counterparty accounts for a LoanSet, reading the
 * LoanBroker owner from the broker entry.
 *
 * The counterparty is the explicit Counterparty field if present, otherwise
 * the LoanBroker owner. In the two-step (Borrower) flow the borrower is the
 * named Borrower; in the immediate flow the borrower is whichever of the
 * signer / counterparty is not the LoanBroker owner.
 *
 * @param tx The LoanSet transaction being applied.
 * @param brokerSle The LoanBroker ledger entry.
 * @param signingAccount The account that signed the transaction.
 * @param flow The flow the transaction is exercising.
 *
 * @return The resolved borrower and counterparty accounts.
 */
Participants
resolveParticipants(
    STTx const& tx,
    SLE::const_ref brokerSle,
    AccountID const& signingAccount,
    LoanFlow flow)
{
    AccountID const brokerOwner = brokerSle->at(sfOwner);
    auto const counterparty = tx[~sfCounterparty].value_or(brokerOwner);

    AccountID const borrower = [&]() -> AccountID {
        if (flow == LoanFlow::TwoStep)
            return tx[sfBorrower];
        return counterparty == brokerOwner ? signingAccount : counterparty;
    }();
    return Participants{.borrower = borrower, .counterparty = counterparty};
}

/**
 * Reads the LoanBroker and Vault entries, validates the requested loan
 * against them, and computes the loan properties and derived values.
 *
 * @param ctx The apply context for the transaction.
 * @param j Log.
 *
 * @return The validated and computed LoanSetup on success, or the TER
 * describing why the loan cannot be created on failure.
 */
std::expected<LoanSetup, TER>
setupLoan(ApplyContext& ctx, beast::Journal const& j)
{
    auto const& tx = ctx.tx;
    auto& view = ctx.view();

    auto const brokerID = tx[sfLoanBrokerID];

    // Only the LoanBroker and Vault entries are read here; setupLoan() validates
    // the loan against them and computes the plan inputs. The broker owner,
    // borrower, and broker pseudo-account entries are re-fetched (and their
    // existence re-verified) by the flow functions that actually mutate them, so
    // they are not peeked here. Borrower existence is already guaranteed by
    // preclaim().
    auto const brokerSle = view.peek(keylet::loanBroker(brokerID));
    if (!brokerSle)
        return std::unexpected(tefBAD_LEDGER);  // LCOV_EXCL_LINE

    auto const vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return std::unexpected(tefBAD_LEDGER);  // LCOV_EXCL_LINE
    Asset const vaultAsset = vaultSle->at(sfAsset);

    auto const principalRequested = tx[sfPrincipalRequested];

    auto vaultAvailableProxy = vaultSle->at(sfAssetsAvailable);
    auto vaultTotalProxy = vaultSle->at(sfAssetsTotal);
    auto const vaultScale = getAssetsTotalScale(vaultSle);
    if (vaultAvailableProxy < principalRequested)
    {
        JLOG(j.warn()) << "Insufficient assets available in the Vault to fund the loan.";
        return std::unexpected(tecINSUFFICIENT_FUNDS);
    }

    TenthBips32 const interestRate{tx[~sfInterestRate].value_or(0)};

    auto const paymentInterval = tx[~sfPaymentInterval].value_or(LoanSet::kDefaultPaymentInterval);
    auto const paymentTotal = tx[~sfPaymentTotal].value_or(LoanSet::kDefaultPaymentTotal);

    auto const properties = computeLoanProperties(
        view.rules(),
        vaultAsset,
        principalRequested,
        interestRate,
        paymentInterval,
        paymentTotal,
        TenthBips16{brokerSle->at(sfManagementFeeRate)},
        vaultScale);

    LoanState const state = constructLoanState(
        properties.loanState.valueOutstanding,
        principalRequested,
        properties.loanState.managementFeeDue);

    auto const vaultMaximum = *vaultSle->at(sfAssetsMaximum);
    XRPL_ASSERT_PARTS(
        vaultMaximum == 0 || vaultMaximum > *vaultTotalProxy,
        "xrpl::LoanSet::doApply",
        "Vault is below maximum limit");
    if (vaultMaximum != 0 && state.interestDue > vaultMaximum - vaultTotalProxy)
    {
        JLOG(j.warn()) << "Loan would exceed the maximum assets of the vault";
        return std::unexpected(tecLIMIT_EXCEEDED);
    }
    // Check that relevant values won't lose precision. This is mostly only
    // relevant for IOU assets.
    for (auto const& field : LoanSet::getValueFields())
    {
        if (auto const value = tx[field];
            value && !isRounded(vaultAsset, *value, properties.loanScale))
        {
            JLOG(j.warn()) << field.f->getName() << " (" << *value
                           << ") has too much precision. Total loan value is "
                           << properties.loanState.valueOutstanding << " with a scale of "
                           << properties.loanScale;
            return std::unexpected(tecPRECISION_LOSS);
        }
    }

    if (auto const ret = checkLoanGuards(
            vaultAsset,
            principalRequested,
            interestRate != beast::kZero,
            paymentTotal,
            properties,
            j))
        return std::unexpected(ret);

    // Check that the other computed values are valid
    if (properties.loanState.managementFeeDue < 0 || properties.loanState.valueOutstanding <= 0 ||
        properties.periodicPayment <= 0)
    {
        // LCOV_EXCL_START
        JLOG(j.warn()) << "Computed loan properties are invalid. Does not compute."
                       << " Management fee: " << properties.loanState.managementFeeDue
                       << ". Total Value: " << properties.loanState.valueOutstanding
                       << ". PeriodicPayment: " << properties.periodicPayment;
        return std::unexpected(tecINTERNAL);
        // LCOV_EXCL_STOP
    }

    auto const originationFee = tx[~sfLoanOriginationFee].value_or(Number{});

    auto const newDebtDelta = principalRequested + state.interestDue;
    auto const newDebtTotal = brokerSle->at(sfDebtTotal) + newDebtDelta;
    if (auto const debtMaximum = brokerSle->at(sfDebtMaximum);
        debtMaximum != 0 && debtMaximum < newDebtTotal)
    {
        JLOG(j.warn()) << "Loan would exceed the maximum debt limit of the LoanBroker.";
        return std::unexpected(tecLIMIT_EXCEEDED);
    }
    TenthBips32 const coverRateMinimum{brokerSle->at(sfCoverRateMinimum)};
    {
        auto const minCover = [&]() {
            if (ctx.view().rules().enabled(fixCleanup3_2_0))
            {
                return minimumBrokerCover(newDebtTotal, coverRateMinimum, vaultSle);
            }

            // Round the minimum required cover up to be conservative. This ensures
            // CoverAvailable never drops below the theoretical minimum, protecting
            // the broker's solvency.
            NumberRoundModeGuard const mg(Number::RoundingMode::Upward);
            return tenthBipsOfValue(newDebtTotal, coverRateMinimum);
        }();
        if (brokerSle->at(sfCoverAvailable) < minCover)
        {
            JLOG(j.warn()) << "Insufficient first-loss capital to cover the loan.";
            return std::unexpected(tecINSUFFICIENT_FUNDS);
        }
    }

    return LoanSetup{
        .brokerID = brokerID,
        .brokerSle = brokerSle,
        .principalRequested = principalRequested,
        .originationFee = originationFee,
        .interestDue = state.interestDue,
        .properties = properties,
        .paymentInterval = paymentInterval,
        .paymentTotal = paymentTotal};
}

/**
 * Build the Loan ledger entry from the plan, setting the pending flag when
 * requested. Does not insert the entry into the view.
 *
 * @param ctx The apply context for the transaction.
 * @param plan The validated and computed values for the loan.
 * @param brokerSle The LoanBroker ledger entry.
 * @param pending Whether the loan should be flagged as pending.
 *
 * @return The newly built Loan ledger entry.
 */
std::shared_ptr<SLE>
buildLoan(ApplyContext& ctx, LoanPlan const& plan, SLE::ref brokerSle, bool pending)
{
    auto const& tx = ctx.tx;

    // Get shortcuts to the loan property values
    auto const startDate = getStartDate(ctx.view(), tx);
    auto const loanSequence = *brokerSle->at(sfLoanSequence);

    // Create the loan
    auto loan = std::make_shared<SLE>(keylet::loan(plan.brokerID, loanSequence));

    // Prevent copy/paste errors
    auto setLoanField = [&loan, &tx](auto const& field, std::uint32_t const defValue = 0) {
        // at() is smart enough to unseat a default field set to the default
        // value
        loan->at(field) = tx[field].value_or(defValue);
    };

    // Set required and fixed tx fields
    loan->at(sfLoanScale) = plan.properties.loanScale;
    loan->at(sfStartDate) = startDate;
    loan->at(sfPaymentInterval) = plan.paymentInterval;
    loan->at(sfLoanSequence) = loanSequence;
    loan->at(sfLoanBrokerID) = plan.brokerID;
    loan->at(sfBorrower) = plan.borrower;
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
    setLoanField(~sfGracePeriod, LoanSet::kDefaultGracePeriod);
    // Set dynamic / computed fields to their initial values
    loan->at(sfPrincipalOutstanding) = plan.principalRequested;
    loan->at(sfPeriodicPayment) = plan.properties.periodicPayment;
    loan->at(sfTotalValueOutstanding) = plan.properties.loanState.valueOutstanding;
    loan->at(sfManagementFeeOutstanding) = plan.properties.loanState.managementFeeDue;
    loan->at(sfPreviousPaymentDueDate) = 0;
    loan->at(sfNextPaymentDueDate) = startDate + plan.paymentInterval;
    loan->at(sfPaymentRemaining) = plan.paymentTotal;
    if (pending)
        loan->setFlag(lsfLoanPending);

    return loan;
}

/**
 * Create a pending loan for the two-step flow: charge the broker owner the
 * owner reserve, create the loan flagged pending, reserve the principal in
 * the vault, and link the loan into the broker directory only.
 *
 * @param ctx The apply context for the transaction.
 * @param accountID The account that submitted the transaction.
 * @param preFeeBalance The account balance before the transaction fee.
 * @param plan The validated and computed values for the loan.
 * @param j Log.
 *
 * @return tesSUCCESS on success, otherwise the error code describing the
 * failure.
 */
TER
applyPendingLoan(
    ApplyContext& ctx,
    AccountID accountID,
    XRPAmount preFeeBalance,
    LoanPlan const& plan,
    beast::Journal const& j)
{
    auto& view = ctx.view();

    // Re-fetch the ledger entries doApply() already verified exist.
    auto const brokerSle = view.peek(keylet::loanBroker(plan.brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    AccountID const brokerOwner = brokerSle->at(sfOwner);
    auto const brokerOwnerSle = view.peek(keylet::account(brokerOwner));
    if (!brokerOwnerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Values derived from the ledger entries and the plan's scalars.
    AccountID const brokerPseudo = brokerSle->at(sfAccount);
    Asset const vaultAsset = vaultSle->at(sfAsset);
    auto const vaultScale = getAssetsTotalScale(vaultSle);
    auto const newDebtDelta = plan.principalRequested + plan.interestDue;

    // In the two-step flow, the LoanBroker.Owner is charged the owner reserve
    // for the pending loan; the borrower is not charged and receives no funds
    // until the loan is accepted (see LoanAccept).
    if (auto const ter =
            reserveLoanOwner(view, brokerOwner, brokerOwnerSle, accountID, preFeeBalance, j))
        return ter;

    auto loan = buildLoan(ctx, plan, brokerSle, /*pending=*/true);
    view.insert(loan);

    // Update the balances in the vault. Decrement the available assets, accrue
    // the interest due, and move the principal into the reserved bucket until
    // the borrower accepts.
    auto vaultAssetReservedProxy = vaultSle->at(sfAssetsReserved);
    auto vaultAvailableProxy = vaultSle->at(sfAssetsAvailable);
    auto vaultTotalProxy = vaultSle->at(sfAssetsTotal);
    vaultAvailableProxy -= plan.principalRequested;
    vaultTotalProxy += plan.interestDue;
    vaultAssetReservedProxy += plan.principalRequested;
    XRPL_ASSERT_PARTS(
        *vaultAvailableProxy <= *vaultTotalProxy,
        "xrpl::LoanSet::applyPendingLoan",
        "assets available must not be greater than assets outstanding");
    view.update(vaultSle);

    // Update the balances in the loan broker
    adjustImpreciseNumber(brokerSle->at(sfDebtTotal), newDebtDelta, vaultAsset, vaultScale);
    adjustLoanBrokerOwnerCount(view, brokerSle, 1, j);
    auto loanSequenceProxy = brokerSle->at(sfLoanSequence);
    loanSequenceProxy += 1;
    // The sequence should be extremely unlikely to roll over, but fail if it
    // does
    if (loanSequenceProxy == 0)
        return tecMAX_SEQUENCE_REACHED;
    view.update(brokerSle);

    // Link the loan into the broker's directory. The borrower directory link is
    // deferred to LoanAccept for the two-step (pending) flow.
    if (auto const ter = dirLink(view, brokerPseudo, loan, sfLoanBrokerNode))
        return ter;

    associateAsset(*vaultSle, vaultAsset);
    associateAsset(*brokerSle, vaultAsset);
    associateAsset(*loan, vaultAsset);

    return tesSUCCESS;
}

/**
 * Create an active loan for the immediate flow: charge the borrower the
 * owner reserve, disburse the funds, create the loan, update the vault, and
 * link the loan into both the broker and borrower directories.
 *
 * @param ctx The apply context for the transaction.
 * @param accountID The account that submitted the transaction.
 * @param preFeeBalance The account balance before the transaction fee.
 * @param plan The validated and computed values for the loan.
 * @param j Log.
 *
 * @return tesSUCCESS on success, otherwise the error code describing the
 * failure.
 */
TER
applyImmediateLoan(
    ApplyContext& ctx,
    AccountID accountID,
    XRPAmount preFeeBalance,
    LoanPlan const& plan,
    beast::Journal const& j)
{
    auto& view = ctx.view();

    // Re-fetch the ledger entries doApply() already verified exist.
    auto const brokerSle = view.peek(keylet::loanBroker(plan.brokerID));
    if (!brokerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    AccountID const brokerOwner = brokerSle->at(sfOwner);
    auto const brokerOwnerSle = view.peek(keylet::account(brokerOwner));
    if (!brokerOwnerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    auto const borrowerSle = view.peek(keylet::account(plan.borrower));
    if (!borrowerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // Values derived from the ledger entries and the plan's scalars.
    AccountID const brokerPseudo = brokerSle->at(sfAccount);
    AccountID const vaultPseudo = vaultSle->at(sfAccount);
    Asset const vaultAsset = vaultSle->at(sfAsset);
    auto const vaultScale = getAssetsTotalScale(vaultSle);
    auto const loanAssetsToBorrower = plan.principalRequested - plan.originationFee;
    auto const newDebtDelta = plan.principalRequested + plan.interestDue;

    // In the immediate flow, the borrower is charged the owner reserve and the
    // funds are disbursed now.
    if (auto const ter =
            reserveLoanOwner(view, plan.borrower, borrowerSle, accountID, preFeeBalance, j))
        return ter;

    // Disburse the principal to the borrower and the origination fee, if any,
    // to the broker owner, creating holdings as necessary.
    auto applyViewContext = ctx.getApplyViewContext();
    if (auto const ter = disburseLoan(
            applyViewContext,
            borrowerSle,
            brokerOwnerSle,
            vaultPseudo,
            vaultAsset,
            loanAssetsToBorrower,
            plan.originationFee,
            accountID,
            plan.counterparty,
            j))
        return ter;

    auto loan = buildLoan(ctx, plan, brokerSle, /*pending=*/false);
    view.insert(loan);

    // Update the balances in the vault. Decrement the available assets and
    // accrue the interest due.
    auto vaultAvailableProxy = vaultSle->at(sfAssetsAvailable);
    auto vaultTotalProxy = vaultSle->at(sfAssetsTotal);
    vaultAvailableProxy -= plan.principalRequested;
    vaultTotalProxy += plan.interestDue;
    XRPL_ASSERT_PARTS(
        *vaultAvailableProxy <= *vaultTotalProxy,
        "xrpl::LoanSet::applyImmediateLoan",
        "assets available must not be greater than assets outstanding");
    view.update(vaultSle);

    // Update the balances in the loan broker
    adjustImpreciseNumber(brokerSle->at(sfDebtTotal), newDebtDelta, vaultAsset, vaultScale);
    adjustLoanBrokerOwnerCount(view, brokerSle, 1, j);
    auto loanSequenceProxy = brokerSle->at(sfLoanSequence);
    loanSequenceProxy += 1;
    // The sequence should be extremely unlikely to roll over, but fail if it
    // does
    if (loanSequenceProxy == 0)
        return tecMAX_SEQUENCE_REACHED;
    view.update(brokerSle);

    // Link the loan into the broker's directory, then make the borrower the
    // owner of the loan by linking it into the borrower's directory.
    if (auto const ter = dirLink(view, brokerPseudo, loan, sfLoanBrokerNode))
        return ter;

    if (auto const ter = dirLink(view, plan.borrower, loan, sfOwnerNode))
        return ter;

    associateAsset(*vaultSle, vaultAsset);
    associateAsset(*brokerSle, vaultAsset);
    associateAsset(*loan, vaultAsset);

    return tesSUCCESS;
}
}  // namespace

bool
LoanSet::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!checkLendingProtocolDependencies(ctx.rules, ctx.tx))
        return false;

    // The two-step (Borrower) flow fields (Borrower / StartDate) require the
    // two-step flow to be enabled.
    bool const hasBorrowerOrStartDate =
        ctx.tx.isFieldPresent(sfBorrower) || ctx.tx.isFieldPresent(sfStartDate);
    return isTwoStepFlowEnabled(ctx.rules) || !hasBorrowerOrStartDate;
}

std::uint32_t
LoanSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfLoanSetMask;
}

NotTEC
LoanSet::preflight(PreflightContext const& ctx)
{
    using namespace Lending;

    auto const& tx = ctx.tx;

    if (tx.isFieldPresent(sfSponsorFlags) && isReserveSponsored(tx))
    {
        JLOG(ctx.j.debug()) << "LoanSet: reserve sponsorship is not allowed.";
        return temINVALID_FLAG;
    }

    // Special case for Batch inner transactions
    if (tx.isFlag(tfInnerBatchTxn) && ctx.rules.enabled(featureBatchV1_1) &&
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

    bool const twoStepFlowEnabled = isTwoStepFlowEnabled(ctx.rules);
    if (getLoanFlow(tx, twoStepFlowEnabled) == LoanFlow::Invalid)
    {
        // Before the two-step (Borrower) flow was introduced by V1.1, a
        // CounterpartySignature was mandatory for every non-batch transaction.
        if (!twoStepFlowEnabled)
        {
            JLOG(ctx.j.warn()) << "LoanSet transaction must have a CounterpartySignature.";
            return temBAD_SIGNER;
        }
        JLOG(ctx.j.warn()) << "LoanSet transaction must specify either a Borrower with a "
                              "StartDate or a CounterpartySignature.";
        return temINVALID;
    }

    if (counterPartySig)
    {
        if (auto const ret = xrpl::detail::preflightCheckSigningKey(*counterPartySig, ctx.j))
            return ret;
    }

    if (auto const data = tx[~sfData];
        data && !data->empty() && !validDataLength(tx[~sfData], kMaxDataPayloadLength))
        return temINVALID;
    for (auto const& field : {&sfLoanServiceFee, &sfLatePaymentFee, &sfClosePaymentFee})
    {
        if (!validNumericMinimum(tx[~*field]))
            return temINVALID;
    }
    // Principal Requested is required
    auto const p = tx[sfPrincipalRequested];
    if (p <= 0)
        return temINVALID;
    if (!validNumericRange(tx[~sfLoanOriginationFee], p))
        return temINVALID;
    if (!validNumericRange(tx[~sfInterestRate], kMaxInterestRate))
        return temINVALID;
    if (!validNumericRange(tx[~sfOverpaymentFee], kMaxOverpaymentFee))
        return temINVALID;
    if (!validNumericRange(tx[~sfLateInterestRate], kMaxLateInterestRate))
        return temINVALID;
    if (!validNumericRange(tx[~sfCloseInterestRate], kMaxCloseInterestRate))
        return temINVALID;
    if (!validNumericRange(tx[~sfOverpaymentInterestRate], kMaxOverpaymentInterestRate))
        return temINVALID;

    if (auto const paymentTotal = tx[~sfPaymentTotal]; paymentTotal && *paymentTotal <= 0)
        return temINVALID;

    auto const paymentInterval = tx[~sfPaymentInterval];
    if (!validNumericMinimum(paymentInterval, LoanSet::kMinPaymentInterval))
        return temINVALID;  // Grace period is between min default value and payment interval
    if (auto const gracePeriod = tx[~sfGracePeriod]; !validNumericRange(
            gracePeriod,
            paymentInterval.value_or(LoanSet::kDefaultPaymentInterval),
            kDefaultGracePeriod))
    {
        return temINVALID;
    }

    // Copied from preflight2
    if (counterPartySig)
    {
        if (auto const ret =
                xrpl::detail::preflightCheckSimulateKeys(ctx.flags, *counterPartySig, ctx.j))
            return *ret;
    }

    if (auto const brokerID = ctx.tx[~sfLoanBrokerID]; brokerID && *brokerID == beast::kZero)
        return temINVALID;

    return tesSUCCESS;
}

NotTEC
LoanSet::checkSign(PreclaimContext const& ctx)
{
    if (auto ret = Transactor::checkSign(ctx))
        return ret;

    // In the two-step (Borrower) flow introduced by V1.1 there is no
    // counterparty, so there is no CounterpartySignature to check.
    if (getLoanFlow(ctx.tx, isTwoStepFlowEnabled(ctx.view.rules())) == LoanFlow::TwoStep)
        return tesSUCCESS;

    // Counter signer is optional. If it's not specified, it's assumed to be
    // `LoanBroker.Owner`. Note that we have not checked whether the
    // loanbroker exists at this point.
    auto const counterSigner = [&]() -> std::optional<AccountID> {
        if (auto const c = ctx.tx.at(~sfCounterparty))
            return c;

        if (auto const broker = ctx.view.read(keylet::loanBroker(ctx.tx[sfLoanBrokerID])))
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
        ctx.view, ctx.flags, ctx.parentBatchId, *counterSigner, counterSig, ctx.j);
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
    std::size_t const signerCount = [&counterSig]() -> int {
        // Compute defensively.
        // Assure that "tx" cannot be accessed and cause confusion or miscalculations.
        if (counterSig.isFieldPresent(sfSigners))
            return counterSig.getFieldArray(sfSigners).size();
        return counterSig.isFieldPresent(sfTxnSignature) ? 1 : 0;
    }();

    return normalCost + (signerCount * baseFee);
}

std::vector<OptionaledField<STNumber>> const&
LoanSet::getValueFields()
{
    static std::vector<OptionaledField<STNumber>> const kValueFields{
        ~sfPrincipalRequested,
        ~sfLoanOriginationFee,
        ~sfLoanServiceFee,
        ~sfLatePaymentFee,
        ~sfClosePaymentFee
        // Overpayment fee is really a rate. Don't check it here.
    };

    return kValueFields;
}

TER
LoanSet::preclaim(PreclaimContext const& ctx)
{
    auto const& tx = ctx.tx;

    {
        // Check for numeric overflow of the schedule before we load any
        // objects. The Grace Period for the last payment ends at:
        //     startDate + (paymentInterval * paymentTotal) + gracePeriod.
        // If that value is larger than "maxTime", the value
        // overflows, and we kill the transaction.
        using timeType = decltype(sfNextPaymentDueDate)::type::value_type;
        static_assert(std::is_same_v<timeType, std::uint32_t>);
        constexpr timeType kMaxTime = std::numeric_limits<timeType>::max();
        static_assert(kMaxTime == 4'294'967'295);

        auto const timeAvailable = kMaxTime - getStartDate(ctx.view, tx);

        auto const interval = ctx.tx.at(~sfPaymentInterval).value_or(kDefaultPaymentInterval);
        auto const total = ctx.tx.at(~sfPaymentTotal).value_or(kDefaultPaymentTotal);
        auto const grace = ctx.tx.at(~sfGracePeriod).value_or(kDefaultGracePeriod);

        // The grace period can't be larger than the interval. Check it first,
        // mostly so that unit tests can test that specific case.
        if (grace > timeAvailable)
        {
            JLOG(ctx.j.warn()) << "Grace period exceeds protocol time limit.";
            return tecKILLED;
        }

        if (interval > timeAvailable)
        {
            JLOG(ctx.j.warn()) << "Payment interval exceeds protocol time limit.";
            return tecKILLED;
        }

        if (total > timeAvailable)
        {
            JLOG(ctx.j.warn()) << "Payment total exceeds protocol time limit.";
            return tecKILLED;
        }

        auto const timeLastPayment = timeAvailable - grace;

        if (timeLastPayment / interval < total)
        {
            JLOG(ctx.j.warn()) << "Last payment due date, or grace period for "
                                  "last payment exceeds protocol time limit.";
            return tecKILLED;
        }
    }

    auto const account = tx[sfAccount];
    auto const brokerID = tx[sfLoanBrokerID];

    auto const brokerSle = ctx.view.read(keylet::loanBroker(brokerID));
    if (!brokerSle)
    {
        // This can only be hit if there's a counterparty specified, otherwise
        // it'll fail in the signature check
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }
    auto const brokerOwner = brokerSle->at(sfOwner);
    auto const flow = getLoanFlow(tx, isTwoStepFlowEnabled(ctx.view.rules()));
    bool const twoStepFlow = flow == LoanFlow::TwoStep;
    auto const participants = resolveParticipants(tx, brokerSle, account, flow);

    // Validate the submitter's permission. In the two-step flow the LoanBroker
    // owner proposes the loan on behalf of the named Borrower, so the submitter
    // must be the owner. In the immediate flow either the Borrower or the
    // LoanBroker owner may submit, with the other acting as the counterparty.
    if (account != brokerOwner)
    {
        if (twoStepFlow)
        {
            JLOG(ctx.j.warn()) << "Account is not the owner of the LoanBroker.";
            return tecNO_PERMISSION;
        }

        if (participants.counterparty != brokerOwner)
        {
            JLOG(ctx.j.warn()) << "Neither Account nor Counterparty are the owner "
                                  "of the LoanBroker.";
            return tecNO_PERMISSION;
        }
    }

    auto const borrower = participants.borrower;
    auto const brokerPseudo = brokerSle->at(sfAccount);
    if (auto const borrowerSle = ctx.view.read(keylet::account(borrower)); !borrowerSle)
    {
        // It may not be possible to hit this case, because it'll fail the
        // signature check with terNO_ACCOUNT.
        JLOG(ctx.j.warn()) << "Borrower does not exist.";
        return terNO_ACCOUNT;
    }

    auto const vault = ctx.view.read(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vault)
    {
        // Should be impossible
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    if (vault->at(sfAssetsMaximum) != 0 && vault->at(sfAssetsTotal) >= vault->at(sfAssetsMaximum))
    {
        JLOG(ctx.j.warn()) << "Vault at maximum assets limit. Can't add another loan.";
        return tecLIMIT_EXCEEDED;
    }

    Asset const asset = vault->at(sfAsset);

    auto const vaultPseudo = vault->at(sfAccount);

    // Check that relevant values can be represented as the vault asset type.
    // This check is almost duplicated in doApply, but that check is done after
    // the overall loan scale is known. This is mostly only relevant for
    // integral (non-IOU) types
    for (auto const& field : getValueFields())
    {
        if (auto const value = tx[field]; value && STAmount{asset, *value} != *value)
        {
            JLOG(ctx.j.warn()) << field.f->getName() << " (" << *value
                               << ") can not be represented as a(n) " << to_string(asset) << ".";
            return tecPRECISION_LOSS;
        }
    }

    if (auto const ter = checkLoanFreeze(
            ctx.view, asset, vaultPseudo, brokerPseudo, borrower, brokerOwner, ctx.j))
        return ter;

    if (twoStepFlow)
    {
        // Reject a pending loan up front if the borrower or broker owner (the
        // origination-fee recipient) is not authorised to hold the vault asset,
        // rather than creating a loan that can never be disbursed by LoanAccept.
        // WeakAuth is used because the holdings need not exist yet; they are
        // created at disbursement. This is confined to the two-step flow (gated
        // by featureLendingProtocolV1_1); the immediate flow already fails in
        // doApply if disbursement is not possible.
        if (auto const ter = requireAuth(ctx.view, asset, borrower, AuthType::WeakAuth))
            return ter;
        if (auto const ter = requireAuth(ctx.view, asset, brokerOwner, AuthType::WeakAuth))
            return ter;

        if (hasExpired(ctx.view, tx[~sfStartDate]))
        {
            JLOG(ctx.j.warn()) << "Start date is in the past.";
            return tecEXPIRED;
        }
    }

    return tesSUCCESS;
}

TER
LoanSet::doApply()
{
    auto const setup = setupLoan(ctx_, j_);
    if (!setup)
        return setup.error();

    // Bundle the validated and computed values for the flow functions. The
    // pending (two-step) and immediate flows each own their full sequence of
    // ledger mutations; nothing here is reordered relative to the prior
    // implementation.
    auto const flow = getLoanFlow(ctx_.tx, isTwoStepFlowEnabled(ctx_.view().rules()));
    bool const twoStepFlow = flow == LoanFlow::TwoStep;
    auto const participants = resolveParticipants(ctx_.tx, setup->brokerSle, accountID_, flow);
    LoanPlan const plan{
        .brokerID = setup->brokerID,
        .borrower = participants.borrower,
        .counterparty = participants.counterparty,
        .principalRequested = setup->principalRequested,
        .originationFee = setup->originationFee,
        .interestDue = setup->interestDue,
        .properties = setup->properties,
        .paymentInterval = setup->paymentInterval,
        .paymentTotal = setup->paymentTotal};

    return twoStepFlow ? applyPendingLoan(ctx_, accountID_, preFeeBalance_, plan, j_)
                       : applyImmediateLoan(ctx_, accountID_, preFeeBalance_, plan, j_);
}

void
LoanSet::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
LoanSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

//------------------------------------------------------------------------------

}  // namespace xrpl
