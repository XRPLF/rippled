#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for depositing cover assets into a LoanBroker.
 *
 *  Transfers assets from the broker owner's account into the broker's
 *  pseudo-account, increasing `sfCoverAvailable`. Cover backs broker
 *  obligations (fees and risk coverage) within the lending protocol (XLS-66).
 *
 *  @see LoanBrokerCoverWithdraw, LoanBrokerCoverClawback
 */
class LoanBrokerCoverDeposit : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Normal;

    explicit LoanBrokerCoverDeposit(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Gates the transaction on the lending protocol amendment.
     *
     *  Delegates to `checkLendingProtocolDependencies`. Amendment checks
     *  belong here, not in `preflight`.
     *
     *  @param ctx Preflight context providing ledger rules.
     *  @return `true` if the lending protocol amendment is enabled;
     *      `false` otherwise.
     */
    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    /** Stateless validation of transaction fields.
     *
     *  Rejects a zero `sfLoanBrokerID` (null broker reference) and verifies
     *  that `sfAmount` is positive and passes `isLegalNet`. No ledger access
     *  is performed.
     *
     *  @param ctx Preflight context carrying the transaction fields.
     *  @return `temINVALID` if `sfLoanBrokerID` is zero; `temBAD_AMOUNT` if
     *      `sfAmount` is non-positive or fails `isLegalNet`; `tesSUCCESS`
     *      otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates the deposit against current ledger state (read-only).
     *
     *  Checks in order:
     *  1. The `LoanBroker` object exists (`tecNO_ENTRY`).
     *  2. The sender is the broker's `sfOwner` (`tecNO_PERMISSION`).
     *  3. The broker's associated vault exists (`tefBAD_LEDGER` — indicates
     *     ledger corruption; structurally unreachable under normal conditions).
     *  4. The deposited asset matches the vault's `sfAsset` (`tecWRONG_ASSET`).
     *  5. Asset transfer guards: non-transferable (`canTransfer`), source-side
     *     freeze (`checkFrozen`), deep-freeze on the broker pseudo-account
     *     (`checkDeepFrozen`), and strong authorization (`requireAuth`).
     *  6. The sender holds sufficient spendable balance (`tecINSUFFICIENT_FUNDS`),
     *     treating frozen or unauthorized balances as zero.
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return A `TER` indicating the first failing check, or `tesSUCCESS`.
     *  @note The missing-vault branch is excluded from coverage
     *      (`LCOV_EXCL_*`) because ledger integrity constraints make it
     *      unreachable in practice.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Executes the cover deposit against the mutable ledger view.
     *
     *  Transfers `sfAmount` from the sender to the broker's pseudo-account
     *  via `accountSend` with `WaiveTransferFee::Yes` (cover deposits are
     *  exempt from transfer fees), increments `sfCoverAvailable` on the
     *  `LoanBroker` SLE, and calls `associateAsset` to keep the broker's
     *  asset tracking consistent.
     *
     *  @return `tesSUCCESS` on success; `tecINTERNAL` if the broker or
     *      vault SLE is missing (indicates ledger corruption — unreachable
     *      under normal conditions).
     */
    TER
    doApply() override;

    /** Per-entry hook for transaction-specific invariant checks.
     *
     *  No transaction-specific invariants are currently registered; this
     *  override is a placeholder for future work.
     *
     *  @param isDelete `true` if the entry is being deleted.
     *  @param before SLE state before the transaction, or `nullptr` for new entries.
     *  @param after SLE state after the transaction, or `nullptr` for deleted entries.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Finalizes transaction-specific invariant checks.
     *
     *  No transaction-specific invariants are currently registered; always
     *  returns `true`. Placeholder for future work.
     *
     *  @param tx The transaction being applied.
     *  @param result The `TER` result from `doApply`.
     *  @param fee The XRP fee charged for the transaction.
     *  @param view Read-only view of the ledger after application.
     *  @param j Journal for diagnostic logging.
     *  @return `true` unconditionally.
     */
    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

//------------------------------------------------------------------------------

}  // namespace xrpl
