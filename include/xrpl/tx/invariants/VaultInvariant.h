#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xrpl {

/**
 * @brief Invariants: Vault object and MPTokenIssuance for vault shares
 *
 * - vault deleted and vault created is empty
 * - vault created must be linked to pseudo-account for shares and assets
 * - vault must have MPTokenIssuance for shares
 * - vault without shares outstanding must have no shares
 * - loss unrealized is non-negative and does not exceed the difference between
 *   assets total and assets available
 * - assets available do not exceed assets total
 * - vault deposit increases assets and share issuance, and adds to:
 *   total assets, assets available, shares outstanding
 * - vault withdrawal and clawback reduce assets and share issuance, and
 *   subtracts from: total assets, assets available, shares outstanding
 * - vault set must not alter the vault assets or shares balance
 * - loan set moves the requested principal out of the vault, decreasing assets
 *   available (and the vault balance) by the principal
 * - loan manage never removes assets from the vault: assets available may only
 *   grow (and the vault balance grows with it, by the returned first-loss
 *   capital on a default, which leaves the loan-broker pseudo-account by the
 *   same amount), and assets outstanding may only shrink (realized loss); loss
 *   unrealized moves in the direction of the sub-operation (up on impair, down
 *   on unimpair and on default, as the paper loss is either reversed or
 *   realized); a loan manage with none of the sub-operation flags (impair,
 *   unimpair, default) is a no-op and must not modify the vault
 * - loan pay adds the paid principal and interest to the vault: assets
 *   available (and the vault balance) increase by the same amount, which is at
 *   most the amount paid; the combined inflow to the vault pseudo-account, the
 *   loan-broker pseudo-account and the loan-broker owner never exceeds the
 *   amount paid (no value is manufactured); the amount the paid loan owes to
 *   the vault may only shrink; and assets outstanding move in lock-step: their
 *   change equals the cash received plus the change in the paid loan's amount
 *   owed to the vault (under the vault's accounting basis), which verifies the
 *   payment was split correctly between principal and interest
 * - shares outstanding may only change through deposit, withdraw, or clawback
 * - no vault transaction can change loss unrealized (it's updated by loan
 *   transactions)
 * - a created closed-ended vault must satisfy
 *   MIN_INVESTMENT_PERIOD <= RedemptionDate - SubscriptionDate <
 *   MAX_INVESTMENT_PERIOD
 * - vault deposit may only succeed when the vault phase is NoPhase or
 *   Subscription
 * - vault withdrawal may not succeed when the vault phase is Investment
 * - closed-ended loan origination (ttLOAN_SET) may only succeed when the
 *   vault phase is Investment
 *
 * Immutability of VaultKind, SubscriptionDate and RedemptionDate is enforced
 * by NoModifiedUnmodifiableFields (see InvariantCheck.cpp).
 */
class ValidVault
{
    static constexpr Number kZero{};

    struct Vault final
    {
        uint256 key = beast::kZero;
        Asset asset;
        AccountID pseudoId;
        AccountID owner;
        uint192 shareMPTID = beast::kZero;
        Number assetsTotal = 0;
        Number assetsAvailable = 0;
        Number assetsMaximum = 0;
        Number lossUnrealized = 0;
        // Recognition model (accrual vs. cash-basis) this vault was created
        // with; absent sfLEVersion means the legacy, accrual-basis model.
        VaultVersion version = VaultVersion::Legacy;
        std::optional<std::uint8_t> vaultKind;
        std::optional<std::uint32_t> subscriptionDate;
        std::optional<std::uint32_t> redemptionDate;

        Vault static make(SLE const&);
    };

    struct Shares final
    {
        MPTIssue share;
        std::uint64_t sharesTotal = 0;
        std::uint64_t sharesMaximum = 0;
        std::uint32_t flags = 0;

        Shares static make(SLE const&);
    };

    struct Loan final
    {
        uint256 key = beast::kZero;
        uint256 loanBrokerID = beast::kZero;
        // Borrower of the loan. Snapshotted so the loan-set funding checks can
        // verify the principal, net of the origination fee, was credited to
        // this account.
        AccountID borrower;
        // Origination fee routed to the broker owner when the loan is funded.
        // Absent on the ledger entry when zero; the snapshot normalizes to
        // Number{0} in that case.
        Number originationFee = 0;
        Number principalOutstanding = 0;
        Number totalValueOutstanding = 0;
        Number managementFeeOutstanding = 0;
        // Whether lsfLoanImpaired was set on the ledger entry. Required by the
        // LossUnrealized magnitude checks in finalizeLoanManage, which switch
        // on the pre-transaction impairment state of a defaulted loan.
        bool impaired = false;

        // The value this loan owes to the vault, from the loan's perspective.
        // Accounting-basis dependent: under accrual it is the total value
        // owed less the broker's management fee (which belongs to the broker,
        // not the vault); under cash-basis, where interest is only recognised
        // once received, it is the outstanding principal alone. Mirrors
        // loanVaultExposure in LendingHelpers.cpp.
        [[nodiscard]] Number
        owedToVault(VaultVersion version) const;

        Loan static make(SLE const&);
    };

    // Snapshot of a LoanBroker ledger entry. Populated by visitEntry whenever a
    // broker is touched by the transaction, so the lending-side finalizers can
    // compute deltas on DebtTotal and CoverAvailable without a separate read of
    // the after-state.
    struct Broker final
    {
        uint256 key = beast::kZero;
        AccountID owner;
        uint256 vaultID = beast::kZero;
        Number debtTotal = 0;
        Number coverAvailable = 0;

        Broker static make(SLE const&);
    };

public:
    struct DeltaInfo final
    {
        Number delta = kNumZero;
        std::optional<int> scale;

        // Compute the delta between two Numbers, taking the coarsest scale
        [[nodiscard]] static DeltaInfo
        makeDelta(Number const& before, Number const& after, Asset const& asset);
    };

private:
    std::vector<Vault> afterVault_;
    std::vector<Shares> afterMPTs_;
    std::vector<Loan> afterLoan_;
    std::vector<Broker> afterBroker_;
    std::vector<Vault> beforeVault_;
    std::vector<Shares> beforeMPTs_;
    std::vector<Loan> beforeLoan_;
    std::vector<Broker> beforeBroker_;
    std::unordered_map<uint256, DeltaInfo> deltas_;
    // Share issuance IDs whose MPTokens were touched by this transaction.
    // Populated for every ltMPTOKEN visited. Consumed by the non-transferable
    // vault-shares check in finalize, which needs only to know whether any
    // holder was touched for a given issuance.
    std::unordered_set<uint192> touchedShareIssuances_;

    /**
     * @brief Compute the minimum STAmount scale for rounding invariant
     *        calculations.
     *
     * Post-amendment (@c fixCleanup3_2_0) this is simply the posterior
     * @c assetsTotal scale.  Pre-amendment it is the coarsest scale across
     * @p vaultDelta and both asset-field deltas.
     *
     * @pre @c afterVault_ is non-empty. Under the pre-amendment branch also
     *      @c beforeVault_ is non-empty. Both preconditions are asserted at
     *      runtime and hold for every current caller; the assert catches a
     *      future reuse (e.g. from a new transactor via @c checkLoanFunding)
     *      that reaches this helper without a snapshot.
     *
     * @param vaultDelta Delta of the vault's asset balance for this transaction.
     * @param rules      Active ledger rules (used to check the amendment).
     * @return The minimum scale to apply when rounding vault-related amounts.
     */
    [[nodiscard]] std::int32_t
    computeVaultMinScale(DeltaInfo const& vaultDelta, Rules const& rules) const;

    /**
     * @brief Return the vault-asset balance-change delta for an account.
     *
     * Looks up the ledger-entry delta recorded during @c visitEntry for the
     * account entry (XRP), trust line (IOU), or MPToken (MPT) that corresponds
     * to the vault asset held by @p id.
     *
     * @param id Account whose asset delta is requested.
     * @return The delta, or @c std::nullopt if the entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssets(AccountID const& id) const;

    /**
     * @brief Return the vault-asset delta for the transaction's sending
     *        account, adjusted for the fee.
     *
     * Calls @c deltaAssets for @c tx[sfAccount] and, for non-delegated XRP
     * transactions, adds the consumed fee back so the invariant sees the net
     * asset movement rather than the fee-reduced balance change.
     *
     * @param tx  The transaction being applied.
     * @param fee Fee charged by this transaction.
     * @return The fee-adjusted delta, or @c std::nullopt if the net delta is
     *         zero or the account entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssetsTxAccount(STTx const& tx, XRPAmount fee) const;

    /**
     * @brief Return the vault-share balance-change delta for an account.
     *
     * For the vault's pseudo-account the @c MPTokenIssuance outstanding-amount
     * delta is returned; for all other accounts the @c MPToken delta is
     * returned.
     *
     * @param id Account whose share delta is requested.
     * @return The delta, or @c std::nullopt if the entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaShares(AccountID const& id) const;

    /**
     * @brief Check whether a vault holds no assets.
     *
     * @param vault Snapshot of the vault to test.
     * @return @c true when both @c assetsAvailable and @c assetsTotal are
     *         zero.
     */
    [[nodiscard]] static bool
    isVaultEmpty(Vault const& vault);

    /**
     * @brief Funding-side invariants of a loan-origination transaction.
     *
     * Verifies that the transaction moved the requested principal out of the vault
     * pseudo-account. Under @c featureLendingProtocolV1_1 it also verifies the participant-side
     * accounting: the broker's
     * @c DebtTotal grows by the amount the new loan owes to the vault (basis-aware), the
     * borrower and broker owner receive their respective portions of the principal, and the
     * vault's @c AssetsTotal / @c AssetsAvailable identity holds at origination. Extracted so a
     * future @c LoanAccept transactor can reuse the funding checks independently of the
     * creation-side phase gate.
     *
     * @param tx            The transaction being applied.
     * @param fee           Fee charged by this transaction; added back when the fee-payer is one
     *                      of the participants whose vault-asset flow we compare.
     * @param view          Active ledger view (used for rules).
     * @param j             Journal for logging invariant failures.
     * @return @c true when the funding-side invariants hold.
     */
    [[nodiscard]] bool
    checkLoanFunding(STTx const& tx, XRPAmount fee, ReadView const& view, beast::Journal const& j)
        const;

    /**
     * @brief Invariant check for @c ttLOAN_SET.
     *
     * For a closed-ended vault, a loan may only be originated while the vault is in the Investment
     * phase (strictly past @c SubscriptionDate and before @c RedemptionDate). Open-ended vaults (@c
     * NoPhase) are exempt from the phase gate only; the funding checks apply to every vault kind.
     * The complementary maturity bound (final payment strictly precedes @c RedemptionDate) is
     * enforced by @c ValidLoan.
     *
     * @param tx            The transaction being applied.
     * @param fee           Fee charged by this transaction.
     * @param view          Active ledger view (used for rules).
     * @param j             Journal for logging invariant failures.
     * @return @c true when all @c ttLOAN_SET invariants hold.
     */
    [[nodiscard]] bool
    finalizeLoanSet(STTx const& tx, XRPAmount fee, ReadView const& view, beast::Journal const& j)
        const;

    /**
     * @brief Enforce the invariants specific to a @c ttLOAN_MANAGE
     *        transaction.
     *
     * @param tx            The transaction being applied.
     * @param view          Active ledger view (used for rules).
     * @param j             Journal for logging invariant failures.
     * @return @c true when all @c ttLOAN_MANAGE invariants hold.
     */
    [[nodiscard]] bool
    finalizeLoanManage(STTx const& tx, ReadView const& view, beast::Journal const& j) const;

    /**
     * @brief Enforce the invariants specific to a @c ttLOAN_PAY transaction.
     *
     * @param tx            The transaction being applied.
     * @param view          Active ledger view (used for rules).
     * @param j             Journal for logging invariant failures.
     * @return @c true when all @c ttLOAN_PAY invariants hold.
     */
    [[nodiscard]] bool
    finalizeLoanPay(STTx const& tx, ReadView const& view, beast::Journal const& j) const;

public:
    // Compute the coarsest scale required to represent all numbers
    [[nodiscard]] static std::int32_t
    computeCoarsestScale(std::vector<DeltaInfo> const& numbers);

    void
    visitEntry(bool, SLE::const_ref, SLE::const_ref);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
