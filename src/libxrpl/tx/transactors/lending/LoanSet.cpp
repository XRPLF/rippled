#include <xrpl/tx/transactors/lending/LoanSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
#include <xrpl/ledger/helpers/VaultHelpers.h>
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
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

/**
 * The flow requested by a LoanSet transaction, determined from its fields.
 *
 * OneStep is the immediate flow, where the loan is created and disbursed in
 * a single transaction. TwoStep is the pending (Borrower) flow, where the
 * LoanBroker owner proposes a loan that the named Borrower must later accept.
 * Invalid indicates that the fields do not match either flow shape.
 */
enum class LoanFlow { Invalid, OneStep, TwoStep };

/**
 * Whether a newly-built Loan entry should carry the lsfLoanPending flag.
 */
enum class IsLoanPending { No, Yes };

/**
 * The borrower and counterparty accounts resolved for a LoanSet.
 */
struct Participants
{
    AccountID borrower;
    AccountID counterparty;
};

/**
 * Holds the values validated and computed by setupLoan() that the flow
 * functions need to create the loan and update the ledger.
 *
 * The ledger entries themselves are fetched (and their existence verified)
 * by the flow functions that mutate them; the derived borrower /
 * counterparty account IDs and the pure computed scalars are carried here so
 * they are resolved once, in setupLoan(), rather than in each flow function.
 */
struct LoanPlan
{
    uint256 brokerID;
    AccountID borrower;
    AccountID counterparty;
    Number principalRequested;
    Number originationFee;
    Number interestDue;
    // Accounting deltas resolved once via loanOriginationDeltas(vaultSle, ...)
    // so both the immediate and pending flows apply the same accrual- vs
    // cash-basis dispatch to Vault.AssetsTotal and LoanBroker.DebtTotal.
    Number assetsTotalDelta;
    Number debtTotalDelta;
    LoanProperties properties;
    std::uint32_t startDate{};
    std::uint32_t paymentInterval{};
    std::uint32_t paymentTotal{};
};

std::uint32_t
getCurrentLedgerCloseTime(ReadView const& view)
{
    return view.header().closeTime.time_since_epoch().count();
}

bool
isTwoStepFlowEnabled(Rules const& rules)
{
    return rules.enabled(featureLendingProtocolV1_2);
}

/**
 * Determines which LoanFlow a LoanSet transaction is requesting from its
 * fields. The two-step flow is only available when the corresponding
 * amendment is enabled; when it is not, transactions carrying two-step
 * fields are reported as Invalid.
 */
LoanFlow
getLoanFlow(STTx const& tx, ApplyFlags applyFlags, Rules const& rules)
{
    bool const isBatch = tx.isFlag(tfInnerBatchTxn);
    bool const hasCounterparty = tx.isFieldPresent(sfCounterparty);
    bool const hasCounterpartySignature = tx.isFieldPresent(sfCounterpartySignature);
    bool const hasBorrower = tx.isFieldPresent(sfBorrower);
    bool const hasStartDate = tx.isFieldPresent(sfStartDate);
    bool const hasBorrowerOrStartDate = hasBorrower || hasStartDate;

    bool const twoStepFlowEnabled = isTwoStepFlowEnabled(rules);

    if (twoStepFlowEnabled && hasBorrower && hasStartDate && !hasCounterparty &&
        !hasCounterpartySignature)
        return LoanFlow::TwoStep;
    if ((hasCounterpartySignature || isBatch || (applyFlags & TapProposal) != 0) &&
        !hasBorrowerOrStartDate)
        return LoanFlow::OneStep;
    return LoanFlow::Invalid;
}

/**
 * Returns the loan's start date. In the two-step flow it is the StartDate
 * field named in the proposal; in the immediate flow it is the current ledger
 * close time. Callers pass the LoanFlow they have already determined so the
 * flow is not re-derived here.
 */
std::uint32_t
getStartDate(ReadView const& view, STTx const& tx, LoanFlow flow)
{
    if (flow == LoanFlow::TwoStep)
    {
        return tx[sfStartDate];
    }
    return getCurrentLedgerCloseTime(view);
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
    SLE::ConstRef brokerSle,
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
 * against them, computes the loan properties and derived values, and
 * resolves the borrower / counterparty accounts.
 *
 * @param ctx The apply context for the transaction.
 * @param accountID The account that submitted the transaction.
 * @param flow The flow the transaction is exercising.
 * @param j Log.
 *
 * @return The fully populated LoanPlan on success, or the TER describing
 * why the loan cannot be created on failure.
 */
std::expected<LoanPlan, TER>
setupLoan(ApplyContext& ctx, AccountID const& accountID, LoanFlow flow, beast::Journal const& j)
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

    auto const brokerPseudoSle = view.read(keylet::account(brokerSle->at(sfAccount)));
    if (!brokerPseudoSle)
        return std::unexpected(tefBAD_LEDGER);  // LCOV_EXCL_LINE

    auto const vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return std::unexpected(tefBAD_LEDGER);  // LCOV_EXCL_LINE
    Asset const vaultAsset = vaultSle->at(sfAsset);
    auto const vaultVersion = getVaultVersion(vaultSle);

    auto const principalRequested = tx[sfPrincipalRequested];

    auto vaultAvailableProxy = vaultSle->at(sfAssetsAvailable);
    auto vaultTotalProxy = vaultSle->at(sfAssetsTotal);
    // For Legacy/CashBasis, getVaultBaseScale falls through to getVaultScale.
    auto const vaultScale = getVaultBaseScale(vaultSle);
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

    XRPL_ASSERT_PARTS(
        *vaultSle->at(sfAssetsMaximum) == 0 || vaultVersion >= VaultVersion::CashBasis ||
            *vaultSle->at(sfAssetsMaximum) > *vaultTotalProxy,
        "xrpl::LoanSet::setupLoan",
        "instant-recognition vault is below maximum limit");

    if (loanOriginationExceedsVaultMaximum(vaultSle, vaultTotalProxy, state.interestDue))
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

    if (vaultVersion == VaultVersion::FixedPrecision)
    {
        // Reject origination if this loan's interest would grow the Vault past its Open-zone
        // capacity.
        if (vaultOpenZoneCapacity(vaultSle, state.interestDue) > getVaultOpenLimit(vaultSle))
        {
            JLOG(j.warn()) << "Loan interest would exceed the FixedPrecision Vault's Open zone.";
            return std::unexpected(tecLIMIT_EXCEEDED);
        }
        XRPL_ASSERT(
            properties.loanScale == getVaultBaseScale(vaultSle),
            "xrpl::LoanSet::doApply : FixedPrecision loan uses Vault base scale");
    }

    auto const originationFee = tx[~sfLoanOriginationFee].value_or(Number{});

    auto const [assetsTotalDelta, debtTotalDelta] =
        loanOriginationDeltas(vaultSle, principalRequested, state.interestDue);
    auto const newDebtTotal = brokerSle->at(sfDebtTotal) + debtTotalDelta;
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

    auto const participants = resolveParticipants(tx, brokerSle, accountID, flow);

    return LoanPlan{
        .brokerID = brokerID,
        .borrower = participants.borrower,
        .counterparty = participants.counterparty,
        .principalRequested = principalRequested,
        .originationFee = originationFee,
        .interestDue = state.interestDue,
        .assetsTotalDelta = assetsTotalDelta,
        .debtTotalDelta = debtTotalDelta,
        .properties = properties,
        .startDate = getStartDate(view, tx, flow),
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
SLE::pointer
buildLoan(ApplyContext& ctx, LoanPlan const& plan, SLE::Ref brokerSle, IsLoanPending pending)
{
    auto const& tx = ctx.tx;

    // Get shortcuts to the loan property values
    auto const startDate = plan.startDate;
    auto const loanSequence = *brokerSle->at(sfLoanSequence);

    // Create the loan
    auto loan =
        std::make_shared<SLE>(keylet::loan(plan.brokerID, SeqProxy::rawSequence(loanSequence)));

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
    if (pending == IsLoanPending::Yes)
        loan->setFlag(lsfLoanPending);

    return loan;
}

/**
 * The ledger entries the flow functions mutate, re-fetched after setupLoan()
 * has already verified they exist.
 */
struct LoanEntries
{
    SLE::pointer brokerSle;
    SLE::pointer brokerOwnerSle;
    SLE::pointer vaultSle;
};

/**
 * Peek the LoanBroker, its owner's AccountRoot and the Vault for a loan.
 *
 * @param view The view to peek the entries from.
 * @param brokerID The ID of the LoanBroker the loan belongs to.
 *
 * @return The entries on success, or tefBAD_LEDGER if any is missing.
 */
std::expected<LoanEntries, TER>
peekLoanEntries(ApplyView& view, uint256 const& brokerID)
{
    auto brokerSle = view.peek(keylet::loanBroker(brokerID));
    if (!brokerSle)
        return std::unexpected(tefBAD_LEDGER);  // LCOV_EXCL_LINE
    auto brokerOwnerSle = view.peek(keylet::account(brokerSle->at(sfOwner)));
    if (!brokerOwnerSle)
        return std::unexpected(tefBAD_LEDGER);  // LCOV_EXCL_LINE
    auto vaultSle = view.peek(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vaultSle)
        return std::unexpected(tefBAD_LEDGER);  // LCOV_EXCL_LINE

    return LoanEntries{
        .brokerSle = std::move(brokerSle),
        .brokerOwnerSle = std::move(brokerOwnerSle),
        .vaultSle = std::move(vaultSle)};
}

/**
 * Build the Loan entry, insert it into the view and record it in the ledger:
 * move the principal out of the vault's available assets, apply the
 * assets-total delta, record the broker debt and owner count, advance the
 * broker's loan sequence, link the loan into the broker's directory and
 * associate the vault asset with the entries touched.
 *
 * A pending loan also moves the principal into the vault's reserved bucket
 * until the borrower accepts, and is not linked into the borrower's directory
 * (LoanAccept does that). An active loan is linked into the borrower's
 * directory here, making the borrower its owner.
 *
 * @param ctx The apply context for the transaction.
 * @param plan The validated and computed values for the loan.
 * @param brokerSle The LoanBroker ledger entry.
 * @param vaultSle The Vault ledger entry.
 * @param pending Whether the loan is created pending or active.
 * @param j Log.
 *
 * @return tesSUCCESS on success, otherwise the error code describing the
 * failure.
 */
TER
createLoan(
    ApplyContext& ctx,
    LoanPlan const& plan,
    SLE::Ref brokerSle,
    SLE::Ref vaultSle,
    IsLoanPending pending,
    beast::Journal const& j)
{
    auto& view = ctx.view();

    AccountID const brokerPseudo = brokerSle->at(sfAccount);
    Asset const vaultAsset = vaultSle->at(sfAsset);
    auto const vaultScale = getVaultBaseScale(vaultSle);
    auto const vaultVersion = getVaultVersion(vaultSle);

    auto loan = buildLoan(ctx, plan, brokerSle, pending);
    view.insert(loan);

    // Update the balances in the vault. Decrement the available assets and
    // apply the assets-total delta (instant recognition recognizes the
    // interest here; cash-basis leaves the total untouched). A pending loan
    // also moves the principal into the reserved bucket until the borrower
    // accepts.
    auto vaultAvailableProxy = vaultSle->at(sfAssetsAvailable);
    auto vaultTotalProxy = vaultSle->at(sfAssetsTotal);
    auto vaultReservedProxy = vaultSle->at(sfAssetsReserved);

    if (vaultVersion == VaultVersion::FixedPrecision)
    {
        auto const cashDelta = STAmount{vaultAsset, plan.principalRequested};
        auto const assetReserved = pending == IsLoanPending::Yes ? cashDelta : STAmount{};
        auto const yield = pending == IsLoanPending::No ? plan.interestDue : 0;
        if (auto const ter = adjustVaultBalances(
                vaultSle,
                {
                    .cash = -cashDelta,
                    .deployed = plan.debtTotalDelta,
                    .yield = yield,
                    .reserved = assetReserved,
                },
                j);
            !isTesSuccess(ter))
            return ter;
    }
    else
    {
        auto const assetReserved = pending == IsLoanPending::Yes ? plan.principalRequested : 0;
        vaultAvailableProxy -= plan.principalRequested;
        vaultTotalProxy += plan.assetsTotalDelta;
        vaultReservedProxy += assetReserved;
    }

    XRPL_ASSERT_PARTS(
        *vaultAvailableProxy + *vaultReservedProxy <= *vaultTotalProxy,
        "xrpl::LoanSet::createLoan",
        "assets available plus reserved must not exceed assets outstanding");
    view.update(vaultSle);

    // Update the balances in the loan broker
    adjustBrokerDebtTotal(brokerSle, vaultSle, plan.debtTotalDelta, vaultScale);
    adjustLoanBrokerOwnerCount(view, brokerSle, 1, j);
    auto loanSequenceProxy = brokerSle->at(sfLoanSequence);
    loanSequenceProxy += 1;
    // The sequence should be extremely unlikely to roll over, but fail if it
    // does
    if (loanSequenceProxy == 0)
        return tecMAX_SEQUENCE_REACHED;
    view.update(brokerSle);

    // Link the loan into the broker's directory. An active loan is also linked
    // into the borrower's directory, making the borrower its owner; for a
    // pending loan that link is deferred to LoanAccept.
    if (auto const ter = dirLink(view, brokerPseudo, loan, sfLoanBrokerNode))
        return ter;  // LCOV_EXCL_LINE
    if (pending == IsLoanPending::No)
    {
        if (auto const ter = dirLink(view, plan.borrower, loan, sfOwnerNode))
            return ter;  // LCOV_EXCL_LINE
    }

    associateAsset(*vaultSle, vaultAsset);
    associateAsset(*brokerSle, vaultAsset);
    associateAsset(*loan, vaultAsset);

    return tesSUCCESS;
}
}  // namespace

// StartDate is strictly after SubscriptionDate. A min-gap vault must still
// fit a minimum-interval loan plus kLoanRedemptionBuffer. The interval and
// buffer constants are independent; only their sum (plus the +1 for a
// strictly-later StartDate) is required to fit in kMinInvestmentPeriod.
static_assert(kMinInvestmentPeriod >= LoanSet::kMinPaymentInterval + kLoanRedemptionBuffer + 1);

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
    using namespace lending;

    auto const& tx = ctx.tx;

    if (tx.isFieldPresent(sfSponsorFlags) && isReserveSponsored(tx))
    {
        JLOG(ctx.j.debug()) << "LoanSet: reserve sponsorship is not allowed.";
        return temINVALID_FLAG;
    }

    // 3.8.5.1.3 The transaction is a Batch inner transaction and the Counterparty field is not
    // specified and the Borrower field is not specified. (temBAD_SIGNER)
    if (tx.isFlag(tfInnerBatchTxn) && ctx.rules.enabled(featureBatchV1_1) &&
        !tx.isFieldPresent(sfCounterparty) && !tx.isFieldPresent(sfBorrower))
    {
        auto const parentBatchId = ctx.parentBatchId.value_or(UInt256{0});
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

    if (getLoanFlow(tx, ctx.flags, ctx.rules) == LoanFlow::Invalid)
    {
        // CounterpartySignature is not present and the transaction is not part of a Batch
        // inner transaction and the Borrower field is not specified. (temBAD_SIGNER)
        if (!tx.isFlag(tfInnerBatchTxn) && !counterPartySig && !tx.isFieldPresent(sfBorrower))
        {
            JLOG(ctx.j.warn()) << "LoanSet transaction must have a CounterpartySignature.";
            return temBAD_SIGNER;
        }

        // Both Borrower and Counterparty fields are specified. (temINVALID)
        // Both Borrower and CounterpartySignature fields are specified. (temINVALID)
        JLOG(ctx.j.warn()) << "LoanSet transaction must specify either a Borrower with a "
                              "StartDate or a CounterpartySignature.";
        return temINVALID;
    }

    // In the two-step flow the LoanBroker owner proposes a loan to another
    // account, so the named Borrower must not be the submitting account.
    if (auto const borrower = tx[~sfBorrower]; borrower && *borrower == tx[sfAccount])
    {
        JLOG(ctx.j.warn()) << "LoanSet Borrower must not be the submitting account.";
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

    // In the two-step (Borrower) flow introduced by V1.2 there is no
    // counterparty, so there is no CounterpartySignature to check.
    if (getLoanFlow(ctx.tx, ctx.flags, ctx.view.rules()) == LoanFlow::TwoStep)
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
    auto const interval = ctx.tx.at(~sfPaymentInterval).value_or(kDefaultPaymentInterval);
    auto const total = ctx.tx.at(~sfPaymentTotal).value_or(kDefaultPaymentTotal);
    auto const flow = getLoanFlow(tx, ctx.flags, ctx.view.rules());
    bool const twoStepFlow = flow == LoanFlow::TwoStep;
    auto const startDate = getStartDate(ctx.view, tx, flow);

    {
        // Check for numeric overflow of the schedule before we load any
        // objects. The Grace Period for the last payment ends at:
        //     startDate + (paymentInterval * paymentTotal) + gracePeriod.
        // If that value is larger than "maxTime", the value
        // overflows, and we kill the transaction.
        using TimeType = decltype(sfNextPaymentDueDate)::type::value_type;
        static_assert(std::is_same_v<TimeType, std::uint32_t>);
        constexpr TimeType kMaxTime = std::numeric_limits<TimeType>::max();
        static_assert(kMaxTime == 4'294'967'295);

        auto const timeAvailable = kMaxTime - startDate;
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
        JLOG(ctx.j.warn()) << "LoanBroker does not exist.";
        return tecNO_ENTRY;
    }
    auto const brokerOwner = brokerSle->at(sfOwner);
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
    auto const borrowerSle = ctx.view.read(keylet::account(borrower));
    if (!borrowerSle)
    {
        JLOG(ctx.j.warn()) << "Borrower does not exist.";
        // In the two-step flow the Borrower is a passive field, so treat a
        // missing account like any other missing destination. In the one-step
        // flow the Borrower signs, and checkSign has already rejected it.
        return twoStepFlow ? TER{tecNO_DST} : TER{terNO_ACCOUNT};
    }
    if (twoStepFlow && isPseudoAccount(borrowerSle))
    {
        // A pseudo-account can never sign the LoanAccept, so it cannot be
        // named as the Borrower.
        JLOG(ctx.j.warn()) << "Borrower is a pseudo-account.";
        return tecNO_PERMISSION;
    }

    auto const vault = ctx.view.read(keylet::vault(brokerSle->at(sfVaultID)));
    if (!vault)
    {
        // Should be impossible
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE
    }

    auto const vaultVersion = getVaultVersion(vault);

    if (ctx.view.rules().enabled(featureLendingProtocolV1_1))
    {
        auto const phase = getVaultPhase(ctx.view, vault);
        if (phase == VaultPhase::Subscription)
        {
            JLOG(ctx.j.warn()) << "Vault is still in the subscription phase.";
            return tecTOO_SOON;
        }
        if (phase == VaultPhase::Redemption)
        {
            JLOG(ctx.j.warn()) << "Vault has entered the redemption phase.";
            return tecEXPIRED;
        }
        if (phase == VaultPhase::Investment)
        {
            auto const finalPayment = std::uint64_t{startDate} + (std::uint64_t{interval} * total);
            if (finalPayment + kLoanRedemptionBuffer > vault->at(sfRedemptionDate))
            {
                JLOG(ctx.j.warn())
                    << "Final loan payment date is fewer than " << kLoanRedemptionBuffer
                    << " seconds before the vault's redemption date.";
                return tecNO_PERMISSION;
            }
        }
    }

    // Instant interest recognition credits interestDue into AssetsTotal, so a vault
    // already at AssetsMaximum cannot take another loan. Cash-basis origination
    // does not change AssetsTotal (see cash_basis::loanOriginationDeltas), so
    // this leftover instant-recognition gate must not apply there.
    if (vaultVersion < VaultVersion::CashBasis && vault->at(sfAssetsMaximum) != 0 &&
        vault->at(sfAssetsTotal) >= vault->at(sfAssetsMaximum))
    {
        JLOG(ctx.j.warn()) << "Vault at maximum assets limit. Can't add another loan.";
        return tecLIMIT_EXCEEDED;
    }

    if (vaultVersion == VaultVersion::FixedPrecision)
    {
        // Reject origination if the Vault is already coarsened.
        if (getVaultScale(vault) != getVaultBaseScale(vault))
        {
            JLOG(ctx.j.warn()) << "FixedPrecision Vault is already coarsened; no further loans can "
                                  "be originated until it returns to its base scale.";
            return tecLIMIT_EXCEEDED;
        }
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
            ctx.view,
            asset,
            tx[~sfLoanOriginationFee].value_or(0),
            vaultPseudo,
            brokerPseudo,
            borrower,
            brokerOwner,
            ctx.j))
        return ter;

    if (twoStepFlow)
    {
        // Reject a pending loan up front if a disbursement recipient is not
        // authorised to hold the vault asset, rather than creating a loan that
        // can never be disbursed by LoanAccept. LoanAccept re-checks at
        // acceptance.
        if (auto const ter = checkLoanRecipientAuth(ctx.view, asset, borrower, brokerOwner))
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
    auto const flow = getLoanFlow(ctx_.tx, ctx_.flags(), ctx_.view().rules());
    auto const plan = setupLoan(ctx_, accountID_, flow, j_);
    if (!plan)
        return plan.error();

    auto& view = ctx_.view();

    auto const entries = peekLoanEntries(view, plan->brokerID);
    if (!entries)
        return entries.error();  // LCOV_EXCL_LINE
    auto const& [brokerSle, brokerOwnerSle, vaultSle] = *entries;

    if (flow == LoanFlow::TwoStep)
    {
        // In the two-step flow, the LoanBroker.Owner is charged the owner
        // reserve for the pending loan. The loan is created pending with the
        // principal reserved in the vault; the borrower is not charged and
        // receives no funds until the loan is accepted (see LoanAccept).
        AccountID const brokerOwner = brokerSle->at(sfOwner);
        if (auto const ter =
                reserveLoanOwner(view, brokerOwner, brokerOwnerSle, accountID_, preFeeBalance_, j_))
            return ter;

        return createLoan(ctx_, *plan, brokerSle, vaultSle, IsLoanPending::Yes, j_);
    }

    auto const borrowerSle = view.peek(keylet::account(plan->borrower));
    if (!borrowerSle)
        return tefBAD_LEDGER;  // LCOV_EXCL_LINE

    // In the immediate flow, the borrower is charged the owner reserve and the
    // funds are disbursed now. The loan is created active and owned by the
    // borrower.
    if (auto const ter =
            reserveLoanOwner(view, plan->borrower, borrowerSle, accountID_, preFeeBalance_, j_))
        return ter;

    // Disburse the principal to the borrower and the origination fee, if any,
    // to the broker owner, creating holdings as necessary.
    AccountID const vaultPseudo = vaultSle->at(sfAccount);
    Asset const vaultAsset = vaultSle->at(sfAsset);
    auto applyViewContext = ctx_.getApplyViewContext();
    if (auto const ter = disburseLoan(
            applyViewContext,
            borrowerSle,
            brokerOwnerSle,
            vaultPseudo,
            vaultAsset,
            plan->principalRequested - plan->originationFee,
            plan->originationFee,
            accountID_,
            plan->counterparty,
            j_))
        return ter;

    return createLoan(ctx_, *plan, brokerSle, vaultSle, IsLoanPending::No, j_);
}

void
LoanSet::visitInvariantEntry(bool, SLE::ConstRef, SLE::ConstRef)
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
