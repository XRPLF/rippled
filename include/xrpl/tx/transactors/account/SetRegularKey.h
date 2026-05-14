#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the SetRegularKey transaction type.
 *
 *  Assigns or revokes an account's regular key — a secondary keypair that may
 *  sign transactions in place of the master key. Separating signing authority
 *  from account ownership lets accounts keep their master key in cold storage
 *  while using the regular key for day-to-day operations.
 *
 *  Declared as `Blocker` so that no other transaction from the same account
 *  may be queued ahead of or alongside it: changing the signing key would
 *  invalidate the signatures of any concurrently queued transactions.
 *
 *  @see AccountSet, SignerListSet
 */
class SetRegularKey : public Transactor
{
public:
    /** Marks this transactor as a queue blocker.
     *
     *  A Blocker transaction prevents later transactions from the same account
     *  from being queued until this one settles, because a key change can
     *  invalidate the signatures of any pending transactions.
     */
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Blocker;

    explicit SetRegularKey(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Stateless validity check run before any ledger access.
     *
     *  Rejects the transaction with `temBAD_REGKEY` if `sfRegularKey` equals
     *  `sfAccount` — assigning the account's own ID as its regular key is a
     *  degenerate no-op and almost certainly a mistake.
     *
     *  @param ctx  The preflight context carrying the raw transaction.
     *  @return `temBAD_REGKEY` if `sfRegularKey == sfAccount`; otherwise
     *      `tesSUCCESS`.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Computes the base fee, waiving it for a first-time key rotation.
     *
     *  If the transaction is signed with the account's own master public key
     *  and the account's `lsfPasswordSpent` flag is not yet set, returns
     *  `XRPAmount{0}` — making the transaction free. This bootstrapping
     *  mechanic lets a newly funded account replace an operator-assigned
     *  "password" key with its own key at no cost, lowering the barrier to
     *  entry.  The waiver is consumed exactly once: `doApply` sets
     *  `lsfPasswordSpent` on the same code path.
     *
     *  @param view  Read-only ledger view used to inspect the account SLE.
     *  @param tx    The transaction being evaluated.
     *  @return `XRPAmount{0}` when the one-time waiver applies; otherwise
     *      delegates to `Transactor::calculateBaseFee`.
     */
    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    /** Applies the key change to the account SLE.
     *
     *  **Setting a key**: writes `sfRegularKey` to the account SLE. If the
     *  fee-waiver path was used (i.e., `minimumFee` returns zero), also sets
     *  `lsfPasswordSpent` to prevent a second free rotation.
     *
     *  **Removing a key** (`sfRegularKey` absent in the transaction): enforces
     *  the lockout-prevention invariant — if `lsfDisableMaster` is set and no
     *  multisig signer list exists, removing the regular key would leave the
     *  account with no valid signing path. Returns `tecNO_ALTERNATIVE_KEY` in
     *  that case. Otherwise clears `sfRegularKey` via `makeFieldAbsent`.
     *
     *  @return `tesSUCCESS` on success; `tecNO_ALTERNATIVE_KEY` when removal
     *      would permanently lock the account; `tefINTERNAL` (unreachable in
     *      practice) if the account SLE cannot be found.
     */
    TER
    doApply() override;

    /** Invariant visitor hook — no per-entry checks are defined yet.
     *
     *  @param isDelete  Whether the entry is being deleted.
     *  @param before    SLE state before the transaction.
     *  @param after     SLE state after the transaction.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Invariant finalization hook — no transaction-specific invariants yet.
     *
     *  @param tx      The applied transaction.
     *  @param result  The TER result of `doApply`.
     *  @param fee     The fee charged.
     *  @param view    Read-only view of the post-apply ledger.
     *  @param j       Journal for diagnostic logging.
     *  @return Always `true`; reserved for future invariant checks.
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
