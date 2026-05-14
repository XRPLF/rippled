#include <xrpl/tx/transactors/system/TicketCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <cstdint>
#include <memory>

namespace xrpl {

/** Build a `TxConsequences` that reports the true sequence consumption.
 *
 *  A normal transaction consumes exactly one sequence number. `TicketCreate`
 *  burns `sfTicketCount` sequence numbers in a single shot, so the default
 *  `Normal` factory would cause the transaction queue to undercount the
 *  consumed sequence space and misjudge ordering for subsequent same-account
 *  transactions. Passing `sfTicketCount` as `sequencesConsumed` gives the
 *  queue the correct value.
 *
 *  @param ctx Preflight context providing access to the transaction fields.
 *  @return `TxConsequences` encoding the multi-sequence claim.
 */
TxConsequences
TicketCreate::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx, ctx.tx[sfTicketCount]};
}

/** Validate `sfTicketCount` without accessing ledger state.
 *
 *  Rejects counts outside `[kMIN_VALID_COUNT, kMAX_VALID_COUNT]`. The upper
 *  bound of 250 is a CPU-budget ceiling: benchmarking showed that creating
 *  250 tickets in one transaction takes roughly the same validator time as a
 *  compute-intensive three-path `Payment`, keeping per-transaction cost
 *  predictable across node hardware.
 *
 *  @param ctx Preflight context providing access to the transaction fields.
 *  @return `tesSUCCESS` if the count is within range; `temINVALID_COUNT`
 *      otherwise.
 */
NotTEC
TicketCreate::preflight(PreflightContext const& ctx)
{
    if (std::uint32_t const count = ctx.tx[sfTicketCount];
        count < kMIN_VALID_COUNT || count > kMAX_VALID_COUNT)
        return temINVALID_COUNT;

    return tesSUCCESS;
}

/** Enforce the per-account ticket inventory ceiling before any mutations.
 *
 *  Computes the net ticket delta: `curTicketCount + addedTickets −
 *  consumedTickets`. The `consumedTickets` term is 1 when this transaction
 *  was itself submitted via a ticket (i.e. `getSeqProxy().isTicket()` is
 *  true), otherwise 0. The subtraction prevents a false rejection when an
 *  account exactly at the 250-ticket limit submits a `TicketCreate` via a
 *  ticket to add one more — the net change is zero and the transaction should
 *  succeed. Unsigned underflow cannot occur because `addedTickets >= 1` and
 *  `consumedTickets <= 1`, so the worst case is `addedTickets ==
 *  consumedTickets`, yielding `curTicketCount` unchanged.
 *
 *  @param ctx Preclaim context providing read-only ledger access.
 *  @return `tesSUCCESS` if the net delta stays within `kMAX_TICKET_THRESHOLD`;
 *      `tecDIR_FULL` if the threshold would be exceeded; `terNO_ACCOUNT` if
 *      the submitting account does not exist.
 */
TER
TicketCreate::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];
    auto const sleAccountRoot = ctx.view.read(keylet::account(id));
    if (!sleAccountRoot)
        return terNO_ACCOUNT;

    std::uint32_t const curTicketCount = (*sleAccountRoot)[~sfTicketCount].value_or(0u);
    std::uint32_t const addedTickets = ctx.tx[sfTicketCount];
    std::uint32_t const consumedTickets = ctx.tx.getSeqProxy().isTicket() ? 1u : 0u;

    if (curTicketCount + addedTickets - consumedTickets > kMAX_TICKET_THRESHOLD)
        return tecDIR_FULL;

    return tesSUCCESS;
}

/** Create the requested tickets and update all associated account-root fields.
 *
 *  Execution proceeds in four stages:
 *
 *  1. **Reserve check.** The reserve for `ticketCount` additional owned objects
 *     is computed and compared against `preFeeBalance_` (the balance *before*
 *     the transaction fee was deducted). Using the pre-fee snapshot deliberately
 *     permits the account to consume reserve XRP to cover the fee, while still
 *     requiring that the full reserve for the new tickets exists before fee
 *     payment is considered.
 *
 *  2. **Sequence anchor.** `firstTicketSeq` is read from `sfSequence` on the
 *     account root, which the transaction machinery has already incremented
 *     before `doApply` runs. A defensive guard checks that the transaction's
 *     own `sfSequence` equals `firstTicketSeq − 1` (or is 0 for
 *     ticket-submitted transactions). This invariant is guaranteed by the
 *     framework; the guard is `LCOV_EXCL_LINE` because it should never fire
 *     outside of a framework bug.
 *
 *  3. **Ticket creation loop.** For each of the `ticketCount` tickets, an
 *     `ltTICKET` SLE is allocated with a contiguous sequence number
 *     (`firstTicketSeq + i`), inserted into the ledger, and linked into the
 *     account's owner directory via `dirInsert`. The returned directory page
 *     number is stored in `sfOwnerNode` to enable O(1) deletion later.
 *
 *  4. **Account-root update.** After the loop: `sfTicketCount` is incremented
 *     by `ticketCount` (the field `preclaim` will read on future
 *     `TicketCreate` calls); `adjustOwnerCount` raises `sfOwnerCount` by
 *     `ticketCount`; and `sfSequence` is advanced to `firstTicketSeq +
 *     ticketCount`. This final sequence advance — by more than one — is
 *     unique to `TicketCreate` in the XRPL protocol and is the mechanism by
 *     which those sequence numbers are reserved as tickets rather than left as
 *     gaps.
 *
 *  @return `tesSUCCESS` on success; `tecINSUFFICIENT_RESERVE` if `preFeeBalance_`
 *      cannot cover the new reserve obligation; `tecDIR_FULL` if the owner
 *      directory is exhausted (practically unreachable); `tefINTERNAL` if the
 *      account SLE is missing or the sequence invariant is violated (both
 *      indicate framework bugs and are excluded from coverage analysis).
 */
TER
TicketCreate::doApply()
{
    SLE::pointer const sleAccountRoot = view().peek(keylet::account(account_));
    if (!sleAccountRoot)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const ticketCount = ctx_.tx[sfTicketCount];
    {
        XRPAmount const reserve =
            view().fees().accountReserve(sleAccountRoot->getFieldU32(sfOwnerCount) + ticketCount);

        if (preFeeBalance_ < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    beast::Journal const viewJ{ctx_.registry.get().getJournal("View")};

    std::uint32_t const firstTicketSeq = (*sleAccountRoot)[sfSequence];

    if (std::uint32_t const txSeq = ctx_.tx[sfSequence];
        txSeq != 0 && txSeq != (firstTicketSeq - 1))
        return tefINTERNAL;  // LCOV_EXCL_LINE

    for (std::uint32_t i = 0; i < ticketCount; ++i)
    {
        std::uint32_t const curTicketSeq = firstTicketSeq + i;
        Keylet const ticketKeylet = keylet::kTICKET(account_, curTicketSeq);
        SLE::pointer const sleTicket = std::make_shared<SLE>(ticketKeylet);

        sleTicket->setAccountID(sfAccount, account_);
        sleTicket->setFieldU32(sfTicketSequence, curTicketSeq);
        view().insert(sleTicket);

        auto const page =
            view().dirInsert(keylet::ownerDir(account_), ticketKeylet, describeOwnerDir(account_));

        JLOG(j_.trace()) << "Creating ticket " << to_string(ticketKeylet.key) << ": "
                         << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        sleTicket->setFieldU64(sfOwnerNode, *page);
    }

    std::uint32_t const oldTicketCount = (*(sleAccountRoot))[~sfTicketCount].valueOr(0u);
    sleAccountRoot->setFieldU32(sfTicketCount, oldTicketCount + ticketCount);

    adjustOwnerCount(view(), sleAccountRoot, ticketCount, viewJ);

    // TicketCreate is the only transaction that can advance sfSequence by
    // more than one — this is the mechanism that reserves those sequence
    // numbers as tickets rather than leaving gaps.
    sleAccountRoot->setFieldU32(sfSequence, firstTicketSeq + ticketCount);

    return tesSUCCESS;
}

/** Invariant visitor stub — no per-entry checks implemented yet.
 *
 *  Ticket creation and deletion are already validated by the generic
 *  `AccountRootsDeletedClean` and `XRPNotCreated` framework invariants.
 *  This override exists as a placeholder for any future `ltTICKET`-specific
 *  checks.
 */
void
TicketCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** Invariant finalization stub — unconditionally passes.
 *
 *  No transaction-specific finalization logic is currently required.
 *  Always returns `true` so the invariant framework treats this transactor
 *  as passing its own checks.
 *
 *  @return `true` unconditionally.
 */
bool
TicketCreate::finalizeInvariants(
    STTx const&,
    TER,
    XRPAmount,
    ReadView const&,
    beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
