/** @file
 *  Implements `PaymentChannelFund`, the "refill" transactor for XRPL payment
 *  channels. A channel owner calls this to deposit additional XRP into an
 *  existing channel or to extend its voluntary expiration deadline without
 *  closing and reopening the channel.
 *
 *  The apply phase enforces a specific guard order: expiry-triggered cleanup
 *  runs before the ownership check, so any account touching a stale channel
 *  will garbage-collect it regardless of whether they own it.
 */
#include <xrpl/tx/transactors/payment_channel/PaymentChannelFund.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/PaymentChannelHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <memory>

namespace xrpl {

/** Construct `TxConsequences` that reflect the full XRP deposit, not just the fee.
 *
 *  The transaction queue uses this to account for the XRP locked in flight.
 *  Reporting only the fee would undercount resource consumption and allow
 *  conflicting transactions to be queued that assume the deposited XRP is
 *  still available.
 *
 *  @param ctx  Preflight context providing the transaction fields.
 *  @return     `TxConsequences` with consumed XRP set to `sfAmount.xrp()`.
 */
TxConsequences
PaymentChannelFund::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx, ctx.tx[sfAmount].xrp()};
}

/** Stateless validation: `sfAmount` must be a positive XRP value.
 *
 *  The `sfAmount` field is polymorphic at the protocol level and could carry
 *  a non-XRP (IOU) amount from a malformed transaction; the `isXRP()` guard
 *  rejects those. All ledger-state checks (channel existence, permissions,
 *  reserve) are deferred to `doApply`.
 *
 *  @param ctx  Preflight context providing the transaction and current rules.
 *  @return     `tesSUCCESS`, or `temBAD_AMOUNT` if `sfAmount` is non-XRP or
 *              non-positive.
 */
NotTEC
PaymentChannelFund::preflight(PreflightContext const& ctx)
{
    if (!isXRP(ctx.tx[sfAmount]) || (ctx.tx[sfAmount] <= beast::kZERO))
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

/** Apply the funding operation: validate channel state then mutate it.
 *
 *  Guards are evaluated in this deliberate order:
 *
 *  1. **Channel existence** — looks up the `ltPAYCHAN` SLE; `tecNO_ENTRY` if
 *     absent.
 *  2. **Expiry short-circuit** — if `parentCloseTime` has reached either
 *     `sfCancelAfter` (immutable hard deadline) or `sfExpiration` (mutable soft
 *     deadline), calls `closeChannel()` and returns immediately, *before* the
 *     ownership check. Any account touching an expired channel thus triggers
 *     cleanup — this is how XRPL garbage-collects stale channels without
 *     requiring owner action.
 *  3. **Ownership** — only the original channel creator (`sfAccount` on the
 *     channel SLE) may fund or extend; others receive `tecNO_PERMISSION`.
 *  4. **Expiration extension** — if the transaction carries `sfExpiration`, the
 *     new value must be ≥ `parentCloseTime + sfSettleDelay` (floored to the
 *     existing expiration when it is earlier). This prevents the owner from
 *     sneaking in a very short expiration that deprives the recipient of their
 *     full settle window; violation returns `temBAD_EXPIRATION`.
 *  5. **Reserve check** — owner balance must cover `accountReserve(ownerCount)`;
 *     shortfall returns `tecINSUFFICIENT_RESERVE`. Checked separately from the
 *     balance check to give callers a distinct error code when the account is
 *     already underwater before considering the deposit.
 *  6. **Balance check** — owner balance must also cover reserve + `sfAmount`;
 *     shortfall returns `tecUNFUNDED`.
 *  7. **Destination existence** — destination account must still exist; if it
 *     was deleted after channel creation, adding funds would lock XRP into an
 *     unclaimable channel; returns `tecNO_DST`.
 *
 *  On success, `sfAmount` on the channel SLE (total funded capacity) is
 *  incremented and the owner's account `sfBalance` is decremented by the same
 *  value. Both SLEs are committed via `ctx_.view().update()`.
 *
 *  @return `tesSUCCESS`, or one of `tecNO_ENTRY`, `tecNO_PERMISSION`,
 *          `temBAD_EXPIRATION`, `tecINSUFFICIENT_RESERVE`, `tecUNFUNDED`,
 *          `tecNO_DST`, or `tefINTERNAL` (unreachable in practice — see below).
 *
 *  @note The `tefINTERNAL` path (source account SLE not found) is marked
 *        `LCOV_EXCL_LINE` because a signed, accepted transaction implies the
 *        submitting account must exist; the check is purely defensive.
 */
TER
PaymentChannelFund::doApply()
{
    Keylet const k(ltPAYCHAN, ctx_.tx[sfChannel]);
    auto const slep = ctx_.view().peek(k);
    if (!slep)
        return tecNO_ENTRY;

    AccountID const src = (*slep)[sfAccount];
    auto const txAccount = ctx_.tx[sfAccount];
    auto const expiration = (*slep)[~sfExpiration];

    {
        auto const cancelAfter = (*slep)[~sfCancelAfter];
        auto const closeTime = ctx_.view().header().parentCloseTime.time_since_epoch().count();
        if ((cancelAfter && closeTime >= *cancelAfter) || (expiration && closeTime >= *expiration))
            return closeChannel(slep, ctx_.view(), k.key, ctx_.registry.get().getJournal("View"));
    }

    if (src != txAccount)
    {
        return tecNO_PERMISSION;
    }

    if (auto extend = ctx_.tx[~sfExpiration])
    {
        auto minExpiration = ctx_.view().header().parentCloseTime.time_since_epoch().count() +
            (*slep)[sfSettleDelay];
        if (expiration && *expiration < minExpiration)
            minExpiration = *expiration;

        if (*extend < minExpiration)
            return temBAD_EXPIRATION;
        (*slep)[~sfExpiration] = *extend;
        ctx_.view().update(slep);
    }

    auto const sle = ctx_.view().peek(keylet::account(txAccount));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    {
        auto const balance = (*sle)[sfBalance];
        auto const reserve = ctx_.view().fees().accountReserve((*sle)[sfOwnerCount]);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        if (balance < reserve + ctx_.tx[sfAmount])
            return tecUNFUNDED;
    }

    if (AccountID const dst = (*slep)[sfDestination]; !ctx_.view().read(keylet::account(dst)))
    {
        return tecNO_DST;
    }

    (*slep)[sfAmount] = (*slep)[sfAmount] + ctx_.tx[sfAmount];
    ctx_.view().update(slep);

    (*sle)[sfBalance] = (*sle)[sfBalance] - ctx_.tx[sfAmount];
    ctx_.view().update(sle);

    return tesSUCCESS;
}

/** No-op invariant visitor; no per-entry checks are defined for this transaction type yet. */
void
PaymentChannelFund::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** No-op invariant finalizer; always returns `true` until transaction-specific
 *  invariants are defined.
 */
bool
PaymentChannelFund::finalizeInvariants(
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
