#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/** Transactor for the `TicketCreate` transaction type.
 *
 *  Mints one or more `Ticket` ledger objects in a single transaction.
 *  Tickets reserve a contiguous block of future account sequence numbers,
 *  allowing the owner to submit transactions out-of-order or in parallel
 *  without violating the normal strict sequence increment requirement.
 *
 *  Uses `ConsequencesFactoryType::Custom` so that `makeTxConsequences` can
 *  report the exact number of sequence numbers consumed (equal to
 *  `sfTicketCount`), enabling the transaction queue to correctly order
 *  transactions that reference the newly minted ticket sequences.
 *
 *  @note `TicketCreate` is the only transactor that advances an account's
 *      `sfSequence` by more than one in a single application.
 */
class TicketCreate : public Transactor
{
public:
    static constexpr auto kCONSEQUENCES_FACTORY = ConsequencesFactoryType::Custom;

    /** Minimum number of tickets that may be created in one transaction.
     *
     *  A count of zero is nonsensical and is rejected by `preflight` with
     *  `temINVALID_COUNT`.
     */
    constexpr static std::uint32_t kMIN_VALID_COUNT = 1;

    /** Maximum number of tickets that may be created in one transaction.
     *
     *  Calibrated so that a single `TicketCreate` consuming this many tickets
     *  takes roughly the same validator CPU as a compute-intensive three-path
     *  `Payment`. Benchmarked on a MacBook Pro release build (asserts off):
     *  250 tickets averaged 1.21 ms vs. 1.25 ms for the reference Payment.
     *  Exceeding this value is rejected by `preflight` with `temINVALID_COUNT`.
     *
     *  @note Benchmark performed October 2018.
     */
    constexpr static std::uint32_t kMAX_VALID_COUNT = 250;

    /** Maximum number of tickets an account may hold at one time.
     *
     *  If a `TicketCreate` would push the account's net ticket inventory
     *  (existing + added − consumed) above this threshold, `preclaim` rejects
     *  the transaction with `tecDIR_FULL`. The limit is an anti-ledger-stuffing
     *  measure and was chosen to match `kMAX_VALID_COUNT`.
     */
    constexpr static std::uint32_t kMAX_TICKET_THRESHOLD = 250;

    explicit TicketCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Build the `TxConsequences` for a `TicketCreate` transaction.
     *
     *  Reports `sfTicketCount` as the number of sequence numbers consumed so
     *  the transaction queue can correctly evaluate ordering constraints for
     *  any later transaction that references one of the newly reserved ticket
     *  sequences.
     *
     *  @param ctx Preflight context providing access to the transaction fields.
     *  @return A `TxConsequences` object encoding the multi-sequence claim.
     */
    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Stateless validation of `sfTicketCount`.
     *
     *  Reads `sfTicketCount` from the transaction and rejects values outside
     *  `[kMIN_VALID_COUNT, kMAX_VALID_COUNT]`. No ledger state is accessed.
     *
     *  @param ctx Preflight context providing access to the transaction fields.
     *  @return `tesSUCCESS` if the count is in range; `temINVALID_COUNT`
     *      otherwise.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** State-aware validation of the account's ticket inventory.
     *
     *  Computes the net ticket delta after applying this transaction:
     *  `curTicketCount + addedTickets − consumedTickets`. The `consumedTickets`
     *  term is 1 when the transaction itself was submitted using a ticket
     *  (i.e. `getSeqProxy().isTicket()` is true), otherwise 0.
     *
     *  @param ctx Preclaim context providing read-only ledger access.
     *  @return `tesSUCCESS` if the net delta stays within
     *      `kMAX_TICKET_THRESHOLD`; `tecDIR_FULL` if the threshold would be
     *      exceeded; `terNO_ACCOUNT` if the submitting account does not exist.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Create the requested tickets and update account state.
     *
     *  Execution steps:
     *  1. Checks the account's pre-fee balance against the reserve required
     *     for `ticketCount` additional owned objects, using `preFeeBalance_`
     *     so the account may dip into its reserve to pay the fee.
     *  2. Reads `sfSequence` from the account root (already incremented by the
     *     transaction machinery) as the first ticket sequence number, then
     *     sanity-checks that `txSeq == firstTicketSeq − 1` (or `txSeq == 0`
     *     for ticket-submitted transactions).
     *  3. Creates one `Ticket` SLE per requested ticket, inserts each into the
     *     ledger, and links it into the account's owner directory, storing the
     *     directory page in `sfOwnerNode`.
     *  4. Advances `sfSequence` by `ticketCount` — the only transactor in XRPL
     *     that increments the account sequence by more than one.
     *  5. Increments `sfTicketCount` on the account root and calls
     *     `adjustOwnerCount` with `+ticketCount` to update the reserve
     *     obligation.
     *
     *  @return `tesSUCCESS` on success; `tecINSUFFICIENT_RESERVE` if the
     *      account cannot cover the reserve for the new tickets; `tecDIR_FULL`
     *      if the owner directory is full; `tefINTERNAL` for ledger
     *      corruption (should not occur in practice).
     */
    TER
    doApply() override;

    /** Per-entry invariant visitor (reserved for future use).
     *
     *  No transaction-specific invariants are currently enforced. The override
     *  exists as a placeholder for future checks on `Ticket` SLE mutations.
     *
     *  @param isDelete True if the entry is being deleted.
     *  @param before SLE state before the transaction, or nullptr if new.
     *  @param after SLE state after the transaction, or nullptr if deleted.
     */
    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    /** Post-transaction invariant check (reserved for future use).
     *
     *  No transaction-specific invariants are currently enforced. Always
     *  returns `true`.
     *
     *  @param tx The applied transaction.
     *  @param result The TER produced by `doApply`.
     *  @param fee The fee charged.
     *  @param view Read-only view of the resulting ledger state.
     *  @param j Journal for diagnostic logging.
     *  @return `true` unconditionally; future implementations may return
     *      `false` to signal invariant violations.
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
