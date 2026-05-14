#pragma once

#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

/** Reference fee cost in abstract "fee units" used before the XRPFees amendment.
 *
 *  Prior to `featureXRPFees`, transaction costs were expressed in fee units
 *  rather than drops; a reference transaction cost 10 fee units and the actual
 *  drop cost was determined by multiplying by a per-ledger scaling factor.
 *  After the amendment, fees are expressed natively in drops via `XRPAmount`.
 *
 *  This constant is retained as a compatibility shim: it is written into
 *  `sfReferenceFeeUnits` in validation objects and `fee_ref` in JSON
 *  subscription messages when `featureXRPFees` is not active, preserving the
 *  legacy wire format consumed by older clients.
 */
inline constexpr std::uint32_t kFEE_UNITS_DEPRECATED = 10;

/** Snapshot of a ledger's fee schedule.
 *
 *  Packages the three economically significant fee parameters — transaction
 *  cost, base account reserve, and per-object reserve increment — into a
 *  single value-semantic aggregate. Obtained via `ReadView::fees()` so that
 *  transactors, preflight checks, and RPC handlers can query fee parameters
 *  without knowing the concrete view type.
 *
 *  @invariant Fee parameters are constant within a ledger; changes take
 *      effect only at the next ledger boundary and are driven by validator
 *      fee-vote consensus updating the `FeeSettings` SLE.
 */
struct Fees
{
    /** Minimum fee for a reference transaction, in drops.
     *
     *  Transactions paying fewer drops than this value are rejected.
     *  Zero-initialized so that a default-constructed `Fees` acts as a
     *  safe placeholder in tests or before a ledger is loaded.
     */
    XRPAmount base{0};

    /** Minimum XRP balance every account must hold simply to exist, in drops.
     *
     *  An account whose balance falls below its total reserve (see
     *  `accountReserve()`) becomes reserve-deficient and cannot send payments.
     */
    XRPAmount reserve{0};

    /** Additional reserve required for each ledger object owned by an account,
     *  in drops.
     *
     *  Applies to trust lines, offers, escrows, NFT tokens, and other objects
     *  that consume shared ledger state. Multiplied by `ownerCount` in
     *  `accountReserve()` to produce the total per-object reserve charge.
     */
    XRPAmount increment{0};

    explicit Fees() = default;
    Fees(Fees const&) = default;
    Fees&
    operator=(Fees const&) = default;

    /** Construct a fee schedule from explicit drop amounts.
     *
     *  @param base      Minimum fee for a reference transaction, in drops.
     *  @param reserve   Base account reserve, in drops.
     *  @param increment Per-owned-object reserve increment, in drops.
     */
    Fees(XRPAmount base, XRPAmount reserve, XRPAmount increment)
        : base(base), reserve(reserve), increment(increment)
    {
    }

    /** Compute the total XRP reserve an account must hold, in drops.
     *
     *  Applies the formula `reserve + ownerCount * increment`. Callers
     *  checking whether an account can afford a *new* object should pass
     *  `ownerCount + 1` — the post-creation count — so the check accounts
     *  for the incremental cost of the object being created.
     *
     *  @note Pseudo-accounts (AMM, Vault, LoanBroker) are exempt from
     *      reserves; their callers bypass this method entirely.
     *
     *  @param ownerCount Number of ledger objects currently owned by the
     *      account (from `sfOwnerCount` on the `AccountRoot` SLE).
     *  @return Total required balance in drops.
     */
    [[nodiscard]] XRPAmount
    accountReserve(std::size_t ownerCount) const
    {
        return reserve + ownerCount * increment;
    }
};

}  // namespace xrpl
