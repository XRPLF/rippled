#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Bootstraps a new Automatic Market Maker pool on the XRP Ledger.
 *
 *  `AMMCreate` is the entry point for the AMM DEX subsystem. It creates the
 *  four categories of ledger objects that constitute an AMM pool: a
 *  pseudo-account `AccountRoot` (keyed from the AMM keylet, carrying a
 *  disabled master key and tagged with `sfAMMID`), an `ltAMM` object keyed
 *  by `hash{asset1.currency, asset1.issuer, asset2.currency, asset2.issuer}`
 *  in canonical (`std::minmax`) order, the initial LP tokens computed as
 *  `sqrt(sfAmount * sfAmount2)` and sent to the creator, and the asset
 *  trustlines/MPToken entries required to hold the seeded liquidity.
 *
 *  No other AMM transaction (`AMMDeposit`, `AMMWithdraw`, `AMMVote`,
 *  `AMMBid`, `AMMDelete`) can operate until this transaction succeeds.
 *
 *  @note The fee charged is one owner reserve (not the standard base fee)
 *      because the transaction permanently allocates a scarce ledger object.
 *  @note LP-token trustlines are created with a zero credit limit. A holder
 *      can only receive LP tokens through affirmative action (deposit,
 *      `TrustSet`, offer crossing) — the AMM cannot push tokens to an
 *      unwilling account.
 *  @see [XLS-30d: AMM on XRPL](https://github.com/XRPLF/XRPL-Standards/discussions/78)
 */
class AMMCreate : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit AMMCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gate the transaction on required feature flags.
     *
     *  Returns false (and thereby rejects the transaction before any field
     *  validation) when the AMM subsystem is not yet active on the current
     *  rule set, or when either pool asset is an MPT issue and
     *  `featureMPTokensV2` has not been enabled.
     *
     *  @param ctx  The preflight context providing the current rules and tx.
     *  @return true if all required amendments are active; false otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Validate the transaction fields without ledger access.
     *
     *  Rejects the transaction if the two pool assets are identical
     *  (`temBAD_AMM_TOKENS`), if either amount is structurally invalid
     *  (`invalidAMMAmount`), or if `sfTradingFee` exceeds
     *  `kTRADING_FEE_THRESHOLD` (`temBAD_FEE`).
     *
     *  @param ctx  The preflight context containing the transaction.
     *  @return `tesSUCCESS` on success, or a `tem*` error code on failure.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Return the fee required to submit this transaction.
     *
     *  Overrides the default base-fee logic and returns
     *  `calculateOwnerReserveFee` instead — one owner reserve increment —
     *  because `AMMCreate` permanently allocates a ledger object.
     *
     *  @param view  Read-only ledger view providing the current fee schedule.
     *  @param tx    The transaction being evaluated.
     *  @return Fee in drops equal to one owner reserve increment.
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /** Check ledger state before applying the transaction.
     *
     *  Verifies that no AMM already exists for the asset pair
     *  (`tecDUPLICATE`), that the creator is authorized for both assets,
     *  that neither asset is frozen (`tecFROZEN`), that IOU issuers have
     *  `lsfDefaultRipple` set (`terNO_RIPPLE`), that the creator holds
     *  sufficient XRP for the LP-token trustline reserve plus seed funds
     *  (`tecINSUF_RESERVE_LINE`, `tecUNFUNDED_AMM`), and that neither
     *  seed asset is itself an existing LP token (`tecAMM_INVALID_TOKENS`).
     *  Also validates MPT permissions via `checkMPTTxAllowed`.
     *
     *  When `featureAMMClawback` is not yet active, assets whose issuers
     *  have clawback enabled (`lsfAllowTrustLineClawback` or
     *  `lsfMPTCanClawback`) are rejected with `tecNO_PERMISSION`.
     *
     *  @param ctx  The preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` on success, or an appropriate error `TER`.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Create all AMM ledger objects and seed initial liquidity.
     *
     *  Executes inside a `Sandbox` overlay: the pseudo-account, `ltAMM`
     *  object, LP tokens, and asset trustlines/MPToken entries are created
     *  inside `applyCreate()`. The sandbox is flushed to the live view only
     *  when `applyCreate()` returns `tesSUCCESS`; any earlier failure leaves
     *  the ledger unchanged. On success, both directions of the trading pair
     *  are registered in the `OrderBookDB`.
     *
     *  @return `tesSUCCESS` on success, or a `tec*` error code on failure.
     */
    TER
    doApply() override;

    /** @copydoc Transactor::visitInvariantEntry
     *
     *  Currently a no-op for `AMMCreate`; reserved for future
     *  transaction-specific invariants.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** @copydoc Transactor::finalizeInvariants
     *
     *  Currently returns true unconditionally for `AMMCreate`; reserved for
     *  future transaction-specific post-conditions.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
