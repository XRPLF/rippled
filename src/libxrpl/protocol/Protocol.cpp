/** @file
 *  Predicates that identify the two special ledger milestones built into
 *  the XRPL consensus heartbeat: the **flag ledger** and the **voting
 *  ledger**.
 *
 *  Every `kFLAG_LEDGER_INTERVAL` (256) ledgers the network reaches a
 *  boundary where accumulated validator votes are tallied and network-wide
 *  parameters — fees, reserve requirements, amendment activation, and
 *  Negative UNL reliability scores — are updated.  These two predicates
 *  locate that boundary from different vantage points; the distinction is
 *  resolved entirely at the call site (see `Ledger::isFlagLedger()` and
 *  `Ledger::isVotingLedger()` for the canonical example of the `+1` offset).
 */
#include <xrpl/protocol/Protocol.h>

namespace xrpl {

/** Returns true if @p seq falls exactly on a flag-ledger boundary.
 *
 *  A flag ledger is any ledger whose sequence number is an exact multiple of
 *  `kFLAG_LEDGER_INTERVAL` (256).  It is the ledger in which fee and
 *  amendment votes — collected as validator vote bits in the preceding
 *  validation messages — are applied via pseudo-transactions, and in which
 *  the Negative UNL reliability update takes effect.
 *
 *  Callers pass the ledger's **own** sequence number when asking "has this
 *  ledger already crossed the boundary?"  Compare with `isVotingLedger`,
 *  which callers invoke with `seq + 1` to ask "will the ledger built *on
 *  top of* this one be a flag ledger?".
 *
 *  @param seq  The ledger index to test.
 *  @return `true` if `seq % kFLAG_LEDGER_INTERVAL == 0`.
 *
 *  @note `Change::doApply` and `FeeVoteImpl` gate their parameter-update
 *      logic on this predicate.  Because `kFLAG_LEDGER_INTERVAL` is an
 *      implicit part of the wire protocol, changing it without an amendment
 *      mechanism would cause a hard fork.
 */
bool
isVotingLedger(LedgerIndex seq)
{
    return seq % kFLAG_LEDGER_INTERVAL == 0;
}

/** Returns true if @p seq falls exactly on a flag-ledger boundary.
 *
 *  Semantically distinct from `isVotingLedger` even though the arithmetic is
 *  identical: callers pass `seq + 1` (the sequence of the ledger about to be
 *  built) to ask "is the current consensus session producing a flag ledger?"
 *  That `+1` offset lives at the call site — most visibly in
 *  `Ledger::isVotingLedger()` — so the two names remain meaningful at their
 *  respective call sites without embedding the offset here.
 *
 *  `RCLConsensus` uses both predicates in sequence when assembling
 *  pseudo-transactions for a new consensus round:
 *  - If the previous ledger `isFlagLedger()`, fee-vote and amendment
 *    pseudo-transactions are injected (votes collected during that flag
 *    ledger's validations are applied now).
 *  - If the previous ledger `isVotingLedger()`, Negative UNL
 *    pseudo-transactions are injected (the consensus session is building
 *    the flag ledger itself).
 *
 *  @param seq  The ledger index to test.
 *  @return `true` if `seq % kFLAG_LEDGER_INTERVAL == 0`.
 */
bool
isFlagLedger(LedgerIndex seq)
{
    return seq % kFLAG_LEDGER_INTERVAL == 0;
}
}  // namespace xrpl
