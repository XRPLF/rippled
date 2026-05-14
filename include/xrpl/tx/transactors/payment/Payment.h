#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * Transactor for the `ttPAYMENT` transaction type.
 *
 * Handles three structurally distinct execution paths within a single protocol
 * transaction type: direct XRP-to-XRP transfers, direct MPToken (MPT) transfers
 * (pre-`featureMPTokensV2`), and cross-currency path-based payments routed
 * through `path::RippleCalc`. The branching is an implementation detail hidden
 * from the serialized transaction format.
 *
 * Pipeline stages follow the standard `Transactor` framework:
 * `checkExtraFeatures` → `preflight` → `preflight2` → `preclaim` → `doApply`.
 * All stages except `doApply` are static; compile-time dispatch via
 * `invokePreflight<Payment>` rather than vtable.
 *
 * @note `ConsequencesFactory` is `Custom` because the maximum XRP spend depends
 *     on `sfSendMax` vs. `sfAmount` — the framework invokes `makeTxConsequences`
 *     to obtain a conservative upper bound for queue ordering.
 */
class Payment : public Transactor
{
    /** Maximum number of paths allowed in `sfPaths`. */
    static std::size_t const kMAX_PATH_SIZE = 6;

    /** Maximum number of steps permitted in a single path. */
    static std::size_t const kMAX_PATH_LENGTH = 8;

public:
    /**
     * Signals to the framework that this transactor computes its own
     * `TxConsequences` via `makeTxConsequences` rather than using the
     * standard normal-fee or blocker logic.
     */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Custom;

    explicit Payment(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * Computes the maximum XRP that this transaction could consume.
     *
     * If `sfSendMax` is present and denominated in XRP, its value is the
     * upper bound. Otherwise `sfAmount` is used if it is native. If neither
     * field is XRP, the maximum is zero (no XRP is spent beyond the fee).
     * The result is used by the transaction queue for consequence ordering.
     *
     * @param ctx Stateless preflight context providing the raw transaction.
     * @return `TxConsequences` carrying the computed maximum XRP spend.
     */
    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /**
     * Gates optional transaction fields behind their required amendments.
     *
     * Returns `false` (suppressing the transaction) if:
     * - `sfCredentialIDs` is present but `featureCredentials` is not enabled, or
     * - `sfDomainID` is present but `featurePermissionedDEX` is not enabled.
     *
     * @param ctx Preflight context including the active rule set.
     * @return `true` if all present optional fields are supported; `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /**
     * Returns the set of transaction flags that are valid for this payment.
     *
     * For MPT-denominated payments when `featureMPTokensV2` is not active,
     * only `tfPartialPayment` is permitted beyond the universal flags. Once
     * `MPTokensV2` is active, the full `tfPaymentMask` (which includes
     * `tfLimitQuality`, `tfNoRippleDirect`, etc.) becomes valid because MPT
     * payments can then participate in path-finding.
     *
     * @param ctx Preflight context providing the transaction and active rules.
     * @return Bitmask of flags that must be zero for a well-formed transaction.
     */
    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    /**
     * Performs stateless structural validation of the payment fields.
     *
     * Checks performed (no ledger access):
     * - MPT-denominated `sfAmount` requires `featureMPTokensV1`.
     * - XRP direct payments must not carry `sfSendMax`, `sfPaths`,
     *   `tfPartialPayment`, `tfLimitQuality`, or `tfNoRippleDirect` — each
     *   produces a distinct `temBAD_SEND_XRP_*` error for client diagnostics.
     * - Pre-`MPTokensV2` MPT payments must not carry `sfPaths`.
     * - Self-payments without `sfPaths` are rejected as `temREDUNDANT`;
     *   self-payments with paths are allowed (arbitrage cycle).
     * - When `sfDeliverMin` is set, `tfPartialPayment` must be set, the amount
     *   must be positive, its asset must match `sfAmount`, and it must not
     *   exceed `sfAmount`.
     * - Credential fields are structurally validated via `credentials::checkFields`.
     *
     * @param ctx Preflight context; no ledger view is available.
     * @return `tesSUCCESS` if structurally valid, or a `tem*` error code.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /**
     * Validates delegation permission for the payment when `sfDelegate` is set.
     *
     * Two-tier check:
     * 1. Full permission: if the `DelegateObject` SLE grants blanket `ttPAYMENT`
     *    permission, the payment is allowed unconditionally.
     * 2. Granular permissions: `PaymentMint` permits a delegate to issue tokens
     *    where the delegating account is the asset issuer; `PaymentBurn` permits
     *    sending tokens back to their issuer. Both grants apply only to *direct*
     *    payments — any payment with `sfPaths` set, or where `sfSendMax` refers
     *    to a different asset than `sfAmount`, is denied. This prevents granular
     *    delegation from being used to manipulate multi-hop paths.
     *
     * If `sfDelegate` is absent, returns `tesSUCCESS` immediately.
     *
     * @param view Read-only ledger view used to look up the delegate SLE.
     * @param tx   The transaction being validated.
     * @return `tesSUCCESS` if permitted, `terNO_DELEGATE_PERMISSION` otherwise.
     */
    static NotTEC
    checkPermission(ReadView const& view, STTx const& tx);

    /**
     * Validates ledger-state preconditions for the payment.
     *
     * Checks performed (read-only ledger access):
     * - Non-native payments to a non-existent destination fail `tecNO_DST`;
     *   only XRP can fund a new account.
     * - Partial payments cannot create accounts (`telNO_DST_PARTIAL`).
     * - XRP amount below base reserve cannot create an account (`tecNO_DST_INSUF_XRP`).
     * - Destination with `lsfRequireDestTag` requires `sfDestinationTag`
     *   (`tecDST_TAG_NEEDED`).
     * - Path complexity: more than `kMAX_PATH_SIZE` (6) paths, or any path
     *   exceeding `kMAX_PATH_LENGTH` (8) steps, is rejected `telBAD_PATH_COUNT`.
     * - Credential validity is checked via `credentials::valid`.
     * - If `sfDomainID` is present, both sender and destination must be members
     *   of the specified permissioned domain (`tecNO_PERMISSION`).
     *
     * @param ctx Preclaim context providing a read-only ledger view.
     * @return `tesSUCCESS`, or a `tec*`/`tel*`/`ter*` error code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * Executes the payment and mutates ledger state.
     *
     * Branches into one of three execution paths based on asset type and flags:
     *
     * **RippleCalc (path-based):** Any payment with `sfPaths`, `sfSendMax`, or
     * a non-native destination amount (except direct MPT pre-`MPTokensV2`)
     * routes through `path::RippleCalc::rippleCalculate` inside a
     * `PaymentSandbox`. The sandbox is applied atomically only if the
     * calculation succeeds. `ter*` retry codes from `RippleCalc` are promoted
     * to `tecPATH_DRY` to ensure fee collection and deter path-spam.
     * `ctx_.deliver()` records the actual delivered amount when it differs from
     * the requested amount (enabling the `DeliveredAmount` metadata field).
     *
     * **Direct MPT (pre-`featureMPTokensV2`):** Bypasses `RippleCalc`. Checks
     * `requireAuth`, `canTransfer`, and frozen state, then applies the transfer
     * rate. If `tfPartialPayment` is set and the sender cannot cover the full
     * transfer-rate-adjusted cost, the delivered amount is scaled down and
     * checked against `sfDeliverMin`. `fixMPTDeliveredAmount` gates whether
     * `ctx_.deliver()` is called for partial or rate-adjusted deliveries.
     *
     * **Direct XRP:** Validates that `preFeeBalance_` covers the destination
     * amount plus the reserve (adjusted for whether the source account or a
     * delegate pays the fee). Rejects pseudo-account recipients. Enforces
     * deposit pre-authorization with a small-balance bypass to prevent accounts
     * from becoming permanently wedged. Updates both `sfBalance` fields directly.
     *
     * @return `tesSUCCESS` on success, or a `tec*` error code on failure.
     */
    TER
    doApply() override;

    /**
     * Invariant visitor called once per modified SLE.
     *
     * Currently a no-op placeholder for future transaction-specific invariants.
     *
     * @param isDelete `true` if the SLE is being erased.
     * @param before   SLE state before the transaction, or `nullptr` if new.
     * @param after    SLE state after the transaction, or `nullptr` if deleted.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /**
     * Finalizes transaction-specific invariant checks after all SLEs are visited.
     *
     * Currently a no-op placeholder; always returns `true`.
     *
     * @param tx     The transaction that was applied.
     * @param result The TER code returned by `doApply`.
     * @param fee    The fee charged in drops.
     * @param view   Read-only view of the ledger after the transaction.
     * @param j      Journal for diagnostic logging.
     * @return `true` if all invariants pass; `false` to veto the transaction.
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
