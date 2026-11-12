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
#include <vector>

namespace xrpl {

/**
 * @brief Invariants: Vault object and MPTokenIssuance for vault shares
 *
 * - vault deleted and vault created is empty
 * - vault created must be linked to pseudo-account for shares and assets
 * - vault must have MPTokenIssuance for shares
 * - vault without shares outstanding must have no shares
 * - loss unrealized does not exceed the difference between assets total and
 *   assets available (Legacy/CashBasis), or AssetsDeployed (FixedPrecision)
 * - assets available do not exceed assets total
 * - vault deposit increases assets and share issuance, and adds to:
 *   total assets, assets available, shares outstanding
 * - vault withdrawal and clawback reduce assets and share issuance, and
 *   subtracts from: total assets, assets available, shares outstanding
 * - vault set must not alter the vault assets or shares balance
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
 * - FixedPrecision only: AssetsDeployed is non-negative and exactly
 *   representable at the asset's precision; AssetsDeployed changes only for
 *   ttLOAN_SET, ttLOAN_PAY and ttLOAN_MANAGE; AssetsDeployed is zero right
 *   after ttVAULT_CREATE and whenever shares outstanding return to zero; the
 *   stored AssetsTotal cache equals AssetsAvailable + AssetsDeployed, rounded
 *   Downward to the asset's precision
 * - FixedPrecision only: YieldUnrealized is non-negative; YieldUnrealized
 *   changes only for ttLOAN_SET, ttLOAN_PAY and ttLOAN_MANAGE
 * - from fixCleanup3_6_0 onwards, any transaction that modifies an existing
 *   vault changes assets available by the same amount as the vault
 *   pseudo-account holding: exactly for FixedPrecision, within one unit at
 *   the coarser of the posterior AssetsTotal scale and the holding's own
 *   scale for IOU on Legacy/CashBasis. Before that amendment only deposit,
 *   withdrawal and clawback are checked this way.
 *
 * Immutability of VaultKind, SubscriptionDate and RedemptionDate is enforced
 * by NoModifiedUnmodifiableFields (see InvariantCheck.cpp). From
 * featureLendingProtocolV1_1 onwards, immutability of the vault's Asset,
 * pseudo-account and ShareMPTID is likewise enforced by
 * NoModifiedUnmodifiableFields; prior to that amendment it is checked here.
 */
class ValidVault
{
    static constexpr Number kZero{};

    struct Vault final
    {
        UInt256 key = beast::kZero;
        Asset asset;
        AccountID pseudoId;
        AccountID owner;
        UInt192 shareMPTID = beast::kZero;
        Number assetsTotal = 0;
        Number assetsAvailable = 0;
        Number assetsMaximum = 0;
        Number lossUnrealized = 0;
        Number assetsDeployed = 0;
        Number yieldUnrealized = 0;
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

        Shares static make(SLE const&);
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
    std::vector<Vault> beforeVault_;
    std::vector<Shares> beforeMPTs_;
    std::unordered_map<UInt256, DeltaInfo> deltas_;

    /**
     * @brief Compute the minimum STAmount scale for rounding invariant
     *        calculations.
     *
     * Post-amendment (@c fixCleanup3_2_0) this is simply the posterior
     * @c assetsTotal scale.  Pre-amendment it is the coarsest scale across
     * @p vaultDelta and both asset-field deltas.
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
     * @brief Return the AccountRoot whose XRP balance actually absorbed a
     *        transaction's fee, if any.
     *
     * Mirrors @c Transactor::getFeePayer, but resolves to @c std::nullopt for
     * a pre-funded sponsorship: that fee is drawn from the @c ltSponsorship
     * object's @c sfFeeAmount, never from the sponsor's own AccountRoot, so
     * there is no balance to add back there.
     *
     * @param view Read-only view of the ledger after the transaction.
     * @param tx   The transaction being applied.
     * @return The fee-paying AccountRoot's id, or @c std::nullopt when the
     *         fee was not drawn from any AccountRoot balance.
     */
    [[nodiscard]] static std::optional<AccountID>
    feePayerAccountRoot(ReadView const& view, STTx const& tx);

    /**
     * @brief Return the vault-asset delta for a party inspected as a
     *        withdrawal/deposit counterparty, adjusted for the fee.
     *
     * Calls @c deltaAssets for @p id and, for XRP transactions, adds the
     * consumed fee back only when @p id is the AccountRoot that actually
     * paid it (per @c feePayerAccountRoot) -- so the invariant sees the net
     * asset movement rather than a fee-reduced balance change, regardless of
     * whether @p id is the sender, a distinct destination, a delegate, or a
     * co-signed fee sponsor. Post-@c fixCleanup3_4_0, any resulting
     * economically-zero delta is always normalized to absence.
     *
     * Pre-@c fixCleanup3_4_0 this replicates the legacy behaviour exactly:
     * only @c tx[sfAccount] could ever receive a fee correction (and only
     * when it was itself, per @c STTx::getFeePayerID, the fee payer). After
     * that sender-only correction a zero delta is collapsed to absence; if
     * the correction does not apply, a present-zero delta is kept as-is.
     *
     * @param view          Read-only view of the ledger after the transaction.
     * @param id            Account being inspected as sender or destination.
     * @param tx            The transaction being applied.
     * @param fee           Fee charged by this transaction.
     * @param fix340Enabled Whether @c fixCleanup3_4_0 is enabled, as already
     *                      determined once by @c finalize.
     * @return The fee-adjusted delta, or @c std::nullopt if the net delta is
     *         zero (always post-amendment; pre-amendment only after the
     *         sender-only fee correction) or the entry was not touched.
     */
    [[nodiscard]] std::optional<DeltaInfo>
    deltaAssetsForParty(
        ReadView const& view,
        AccountID const& id,
        STTx const& tx,
        XRPAmount fee,
        bool fix340Enabled) const;

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
     * @brief Invariant check for @c ttLOAN_SET.
     *
     * For a closed-ended vault, a loan may only be originated while the vault is in the Investment
     * phase (strictly past @c SubscriptionDate and before @c RedemptionDate). Open-ended vaults (@c
     * NoPhase) are unaffected. The complementary maturity bound (final payment precedes @c
     * RedemptionDate by at least @c kLoanRedemptionBuffer) is enforced by @c ValidLoan.
     */
    [[nodiscard]] bool
    finalizeLoanSet(ReadView const& view, beast::Journal const& j) const;

    /**
     * @brief Check that a transaction changed an existing vault's
     *        AssetsAvailable by the same amount as its pseudo-account holding.
     *
     * Runs for every successful transaction that modifies an existing vault.
     * A transaction that does not touch the pseudo-account's holding (for
     * example an impair, an unimpair, or a default with no first-loss capital
     * to return) has a zero holding delta, so AssetsAvailable must not change
     * either, beyond the one-unit IOU tolerance on Legacy/CashBasis vaults.
     * See @c assetsAvailableAddsUp for the comparison. The IOU tolerance is
     * one unit at the coarser of the posterior AssetsTotal scale and the
     * holding's own scale, so an existing difference between the holding and
     * AssetsAvailable does not reject a correct transfer.
     *
     * @param rules Active ledger rules.
     * @param j Journal for logging.
     * @return false if AssetsAvailable and the holding moved by different
     *         amounts.
     */
    [[nodiscard]] bool
    checkAssetsAvailableFollowsBalance(Rules const& rules, beast::Journal const& j) const;

    /**
     * @brief Compare a vault's AssetsAvailable delta with the real transfer
     *        into or out of its pseudo-account.
     *
     * FixedPrecision compares the exact deltas. Legacy/CashBasis compare the
     * deltas rounded to @p scale and, when @p tolerateOneUnit is set, accept
     * one unit of difference for IOU (XRP and MPT stay exact).
     *
     * This is the one place that defines how AssetsAvailable relates to the
     * pseudo-account holding. If a vault field that also holds cash is added
     * (such as AssetsReserved), its delta belongs on the AssetsAvailable side
     * here.
     *
     * @param afterVault Snapshot of the vault after the transaction.
     * @param beforeVault Snapshot of the vault before the transaction.
     * @param vaultAsset The vault's underlying asset.
     * @param tolerateOneUnit Whether to accept one unit of IOU rounding noise.
     * @param scale The scale both deltas are rounded to. For
     *        deposit/withdraw/clawback this is the posterior AssetsTotal scale
     *        from computeVaultMinScale; the lending check passes the coarser
     *        tolerance scale (the holding's own trust-line scale when that is
     *        coarser than the AssetsTotal scale).
     * @param roundedVaultDelta The real transfer's delta, rounded to @p scale.
     * @param exactVaultDelta The real transfer's exact (unrounded) delta.
     * @return true if the two deltas agree.
     */
    [[nodiscard]] static bool
    assetsAvailableAddsUp(
        Vault const& afterVault,
        Vault const& beforeVault,
        Asset const& vaultAsset,
        bool tolerateOneUnit,
        std::int32_t scale,
        Number const& roundedVaultDelta,
        Number const& exactVaultDelta);

    /**
     * @brief Check that a vault's AssetsTotal and AssetsAvailable deltas add
     *        up against the real transfer, for deposit/withdraw/clawback.
     *
     * Shared body of the ttVAULT_DEPOSIT, ttVAULT_WITHDRAW and
     * ttVAULT_CLAWBACK "assets outstanding"/"assets available" add-up checks,
     * which differ only in the rounded/exact deltas plugged in and the verb
     * used in the log message.
     *
     * @param afterVault Snapshot of the vault after the transaction.
     * @param beforeVault Snapshot of the vault before the transaction.
     * @param vaultAsset The vault's underlying asset.
     * @param fix340Enabled Whether fixCleanup3_4_0 is enabled.
     * @param minScale The posterior scale computed by computeVaultMinScale.
     * @param roundedVaultDelta The real transfer's delta, rounded to minScale.
     * @param exactVaultDelta The real transfer's exact (unrounded) delta.
     * @param verb The transaction name used in the log message ("deposit",
     *             "withdrawal" or "clawback").
     * @param j Journal for logging.
     * @return false if either add-up check fails.
     */
    [[nodiscard]] static bool
    checkTotalsAddUp(
        Vault const& afterVault,
        Vault const& beforeVault,
        Asset const& vaultAsset,
        bool fix340Enabled,
        std::int32_t minScale,
        Number const& roundedVaultDelta,
        Number const& exactVaultDelta,
        char const* verb,
        beast::Journal const& j);

public:
    // Compute the coarsest scale required to represent all numbers
    [[nodiscard]] static std::int32_t
    computeCoarsestScale(std::vector<DeltaInfo> const& numbers);

    void
    visitEntry(bool, SLE::ConstRef, SLE::ConstRef);

    bool
    finalize(STTx const&, TER const, XRPAmount const, ReadView const&, beast::Journal const&);
};

}  // namespace xrpl
