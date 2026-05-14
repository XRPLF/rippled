#include <xrpl/tx/transactors/token/Clawback.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <variant>

namespace xrpl {

template <ValidIssueType T>
static NotTEC
preflightHelper(PreflightContext const& ctx);

/** Validate IOU clawback transaction fields before any ledger access.
 *
 *  The IOU encoding packs the token holder's account into the `issuer`
 *  sub-field of `sfAmount` rather than a separate `sfHolder` field.
 *  Consequently, `sfHolder` must be absent — its presence is treated as a
 *  malformed transaction to enforce mutual exclusivity between the two
 *  encoding conventions.
 *
 *  Three structural constraints are checked:
 *  - `sfHolder` absent (IOU encoding uses `sfAmount.issuer` for the holder)
 *  - `sfAmount` is not XRP and is strictly positive
 *  - The holder encoded in `sfAmount` differs from `sfAccount` (the issuer)
 *
 *  @param ctx Stateless preflight context.
 *  @return `tesSUCCESS` if valid; `temMALFORMED` if `sfHolder` is present;
 *      `temBAD_AMOUNT` if the amount is XRP, non-positive, or names the
 *      issuer as the holder.
 */
template <>
NotTEC
preflightHelper<Issue>(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfHolder))
        return temMALFORMED;

    AccountID const issuer = ctx.tx[sfAccount];
    STAmount const clawAmount = ctx.tx[sfAmount];

    // The issuer field is used for the token holder instead
    AccountID const& holder = clawAmount.getIssuer();

    if (issuer == holder || isXRP(clawAmount) || clawAmount <= beast::kZERO)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

/** Validate MPT clawback transaction fields before any ledger access.
 *
 *  MPT clawback uses the conventional `sfHolder` field rather than the IOU
 *  encoding trick. The function checks:
 *  - `featureMPTokensV1` is enabled (otherwise the transaction type itself
 *    does not exist on the network)
 *  - `sfHolder` is present and names a different account from `sfAccount`
 *  - `sfAmount` is strictly positive and within `kMAX_MP_TOKEN_AMOUNT`
 *
 *  @param ctx Stateless preflight context.
 *  @return `tesSUCCESS` if valid; `temDISABLED` if the MPT amendment is off;
 *      `temMALFORMED` if `sfHolder` is absent or equals `sfAccount`;
 *      `temBAD_AMOUNT` if the amount is out of range or non-positive.
 */
template <>
NotTEC
preflightHelper<MPTIssue>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    auto const mptHolder = ctx.tx[~sfHolder];
    auto const clawAmount = ctx.tx[sfAmount];

    if (!mptHolder)
        return temMALFORMED;

    // issuer is the same as holder
    if (ctx.tx[sfAccount] == *mptHolder)
        return temMALFORMED;

    if (clawAmount.mpt() > MPTAmount{kMAX_MP_TOKEN_AMOUNT} || clawAmount <= beast::kZERO)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

/** Dispatch to the asset-type-specific preflight helper.
 *
 *  Resolves the runtime `std::variant<Issue, MPTIssue>` inside `sfAmount`
 *  to a compile-time template parameter and forwards to
 *  `preflightHelper<Issue>` or `preflightHelper<MPTIssue>`. No ledger state
 *  is accessed; all checks are purely structural.
 *
 *  @param ctx Stateless preflight context.
 *  @return The result of the appropriate `preflightHelper` specialisation.
 */
NotTEC
Clawback::preflight(PreflightContext const& ctx)
{
    if (auto const ret = std::visit(
            [&]<typename T>(T const&) { return preflightHelper<T>(ctx); },
            ctx.tx[sfAmount].asset().value());
        !isTesSuccess(ret))
        return ret;

    return tesSUCCESS;
}

template <ValidIssueType T>
static TER
preclaimHelper(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    AccountID const& issuer,
    AccountID const& holder,
    STAmount const& clawAmount);

/** Validate ledger preconditions for an IOU clawback.
 *
 *  Checks are applied in this order:
 *
 *  1. **Flag constraints on the issuer**: `lsfAllowTrustLineClawback` must
 *     be set and `lsfNoFreeze` must not be set. These flags are mutually
 *     exclusive by design — an issuer that permanently waived freeze authority
 *     also loses clawback authority because clawback is a strictly stronger
 *     power.
 *
 *  2. **Trust-line existence**: the trust line between `holder` and `issuer`
 *     for the given currency must exist; missing → `tecNO_LINE`.
 *
 *  3. **Balance sign/address ordering invariant**: XRPL stores trust-line
 *     balances with a sign convention keyed to account address ordering. A
 *     positive raw `sfBalance` means the account with the higher address is
 *     the net holder; a negative value means the lower address is the holder.
 *     If the raw balance contradicts the issuer/holder address order, the
 *     transaction is rejected with `tecNO_PERMISSION` to prevent targeting
 *     the wrong side of the trust line.
 *
 *  4. **Non-zero spendable balance**: `accountHolds` with `fhIGNORE_FREEZE`
 *     is used rather than the raw `sfBalance` from the SLE, because the
 *     available balance is subject to additional constraints (e.g., XLS-34
 *     lock-ups) that the raw field does not reflect.
 *
 *  @param ctx Read-only preclaim context.
 *  @param sleIssuer The issuer's `AccountRoot` SLE.
 *  @param issuer The issuer's `AccountID`.
 *  @param holder The holder's `AccountID`.
 *  @param clawAmount The amount specified in the transaction (IOU variant).
 *  @return `tesSUCCESS` on success; `tecNO_PERMISSION` if flag constraints or
 *      balance-sign ordering fail; `tecNO_LINE` if the trust line is absent;
 *      `tecINSUFFICIENT_FUNDS` if the spendable balance is zero.
 */
template <>
TER
preclaimHelper<Issue>(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    AccountID const& issuer,
    AccountID const& holder,
    STAmount const& clawAmount)
{
    std::uint32_t const issuerFlagsIn = sleIssuer.getFieldU32(sfFlags);

    // If AllowTrustLineClawback is not set or NoFreeze is set, return no
    // permission
    if (((issuerFlagsIn & lsfAllowTrustLineClawback) == 0u) ||
        ((issuerFlagsIn & lsfNoFreeze) != 0u))
        return tecNO_PERMISSION;

    auto const sleRippleState =
        ctx.view.read(keylet::line(holder, issuer, clawAmount.get<Issue>().currency));
    if (!sleRippleState)
        return tecNO_LINE;

    STAmount const balance = (*sleRippleState)[sfBalance];

    // If balance is positive, issuer must have higher address than holder
    if (balance > beast::kZERO && issuer < holder)
        return tecNO_PERMISSION;

    // If balance is negative, issuer must have lower address than holder
    if (balance < beast::kZERO && issuer > holder)
        return tecNO_PERMISSION;

    // Must use `accountHolds` rather than reading the raw trust-line balance
    // directly: the spendable balance can be further constrained by features
    // such as XLS-34 that impose holds on top of the nominal sfBalance.
    if (accountHolds(
            ctx.view,
            holder,
            clawAmount.get<Issue>().currency,
            issuer,
            FreezeHandling::IgnoreFreeze,
            ctx.j) <= beast::kZERO)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

/** Validate ledger preconditions for an MPT clawback.
 *
 *  Checks are applied in this order:
 *
 *  1. **MPT issuance object exists**: the `MPTIssuance` SLE identified by
 *     the `MPTIssue` in `sfAmount` must be present in the ledger.
 *
 *  2. **`lsfMPTCanClawback` flag**: the flag is immutable after issuance
 *     creation. If it was not set at creation time the issuer permanently
 *     lacks clawback authority.
 *
 *  3. **Issuer ownership**: the `sfIssuer` field on the issuance object must
 *     match the transaction submitter. This prevents a third party from
 *     constructing a clawback against an issuance they do not own.
 *
 *  4. **Holder `MPToken` object exists**: `keylet::mptoken` must resolve to
 *     an existing ledger object; missing → `tecOBJECT_NOT_FOUND`.
 *
 *  5. **Non-zero spendable balance**: `accountHolds` with both
 *     `fhIGNORE_FREEZE` and `ahIGNORE_AUTH` is used. Authorization status
 *     is irrelevant for a forced reclaim by the issuer.
 *
 *  @param ctx Read-only preclaim context.
 *  @param sleIssuer The issuer's `AccountRoot` SLE (unused; kept for uniform
 *      signature with the `Issue` specialisation).
 *  @param issuer The issuer's `AccountID`.
 *  @param holder The holder's `AccountID`.
 *  @param clawAmount The amount specified in the transaction (MPT variant).
 *  @return `tesSUCCESS` on success; `tecOBJECT_NOT_FOUND` if the issuance or
 *      holder token object is missing; `tecNO_PERMISSION` if the clawback flag
 *      is unset or the caller is not the issuer; `tecINSUFFICIENT_FUNDS` if
 *      the spendable balance is zero.
 */
template <>
TER
preclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    SLE const& sleIssuer,
    AccountID const& issuer,
    AccountID const& holder,
    STAmount const& clawAmount)
{
    auto const issuanceKey = keylet::mptIssuance(clawAmount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (((*sleIssuance)[sfFlags] & lsfMPTCanClawback) == 0u)
        return tecNO_PERMISSION;

    if (sleIssuance->getAccountID(sfIssuer) != issuer)
        return tecNO_PERMISSION;

    if (!ctx.view.exists(keylet::mptoken(issuanceKey.key, holder)))
        return tecOBJECT_NOT_FOUND;

    if (accountHolds(
            ctx.view,
            holder,
            clawAmount.get<MPTIssue>(),
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            ctx.j) <= beast::kZERO)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

/** Validate ledger preconditions for a clawback transaction.
 *
 *  Resolves the holder account ID from the asset-type-dependent encoding
 *  (IOU: from `sfAmount.issuer`; MPT: from `sfHolder`), then applies
 *  protocol-level guards before dispatching to the asset-specific helper.
 *
 *  Order of checks:
 *  1. Both issuer and holder `AccountRoot` SLEs must exist.
 *  2. When `featureSingleAssetVault` is active, the holder must not be a
 *     pseudo-account (`isPseudoAccount` → `tecPSEUDO_ACCOUNT`). When SAV is
 *     active this check subsumes the AMM check that follows.
 *  3. The holder must not be an AMM liquidity-pool account (`sfAMMID` present
 *     → `tecAMM_ACCOUNT`). AMM pool balances are managed via `AMMClawback`
 *     rather than the standard clawback path.
 *  4. Asset-type-specific preconditions via `preclaimHelper<T>`.
 *
 *  @param ctx Read-only preclaim context.
 *  @return `tesSUCCESS` on success; `terNO_ACCOUNT` if either account is
 *      missing; `tecPSEUDO_ACCOUNT` or `tecAMM_ACCOUNT` if the holder is a
 *      protocol-internal account; or any code returned by the type-specific
 *      `preclaimHelper` specialisation.
 */
TER
Clawback::preclaim(PreclaimContext const& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    auto const clawAmount = ctx.tx[sfAmount];
    AccountID const holder = clawAmount.holds<Issue>() ? clawAmount.getIssuer() : ctx.tx[sfHolder];

    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    auto const sleHolder = ctx.view.read(keylet::account(holder));
    if (!sleIssuer || !sleHolder)
        return terNO_ACCOUNT;

    // When SAV is active, isPseudoAccount() also catches AMM accounts, making
    // the sfAMMID check below redundant. Both are kept for defence-in-depth
    // and to preserve the pre-SAV code path without amendment branching.
    if (ctx.view.rules().enabled(featureSingleAssetVault) && isPseudoAccount(sleHolder))
    {
        return tecPSEUDO_ACCOUNT;
    }
    if (sleHolder->isFieldPresent(sfAMMID))
    {
        return tecAMM_ACCOUNT;
    }

    return std::visit(
        [&]<typename T>(T const&) {
            return preclaimHelper<T>(ctx, *sleIssuer, issuer, holder, clawAmount);
        },
        ctx.tx[sfAmount].asset().value());
}

template <ValidIssueType T>
static TER
applyHelper(ApplyContext& ctx);

/** Execute an IOU clawback by transferring tokens from holder to issuer.
 *
 *  The `sfAmount` field as received encodes the holder account in its
 *  `issuer` sub-field (the IOU wire-protocol trick used throughout the
 *  clawback path). Before invoking the transfer, this function corrects the
 *  field by writing the real issuer account ID into `clawAmount.get<Issue>().account`,
 *  so that `directSendNoFee` sees a properly formed amount where `issuer`
 *  identifies the issuer, not the holder.
 *
 *  The amount transferred is `min(spendableAmount, clawAmount)`. The
 *  spendable amount is re-queried via `accountHolds` (with `fhIGNORE_FREEZE`)
 *  at apply time rather than relying on the preclaim result, because ledger
 *  state may have changed between the two phases. This also correctly handles
 *  XLS-34-style holds that reduce the available balance below the nominal
 *  trust-line balance.
 *
 *  Passing `/*checkIssuer*‌/ true` to `directSendNoFee` causes it to verify
 *  issuer involvement in the trust-line accounting, which is appropriate for
 *  the IOU model.
 *
 *  @param ctx Mutable apply context.
 *  @return `tesSUCCESS` on success; `tecINTERNAL` if holder equals issuer
 *      (should never occur after preclaim; LCOV-excluded).
 */
template <>
TER
applyHelper<Issue>(ApplyContext& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    STAmount clawAmount = ctx.tx[sfAmount];
    AccountID const holder = clawAmount.getIssuer();  // cannot be reference

    // Replace the `issuer` field with issuer's account
    clawAmount.get<Issue>().account = issuer;
    if (holder == issuer)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    // Re-query spendable balance at apply time: ledger state may have
    // changed since preclaim, and accountHolds accounts for XLS-34 holds
    // that the raw sfBalance does not reflect.
    STAmount const spendableAmount = accountHolds(
        ctx.view(),
        holder,
        clawAmount.get<Issue>().currency,
        clawAmount.getIssuer(),
        FreezeHandling::IgnoreFreeze,
        ctx.journal);

    return directSendNoFee(
        ctx.view(), holder, issuer, std::min(spendableAmount, clawAmount), true, ctx.journal);
}

/** Execute an MPT clawback by transferring tokens from holder to issuer.
 *
 *  For MPTs the holder account comes from the conventional `sfHolder` field
 *  (not the `sfAmount.issuer` encoding used by IOUs). No field correction is
 *  required before calling the transfer helper.
 *
 *  As with the IOU path, the transfer amount is capped at
 *  `min(spendableAmount, clawAmount)`. The spendable balance is re-queried
 *  at apply time via `accountHolds` with both `fhIGNORE_FREEZE` and
 *  `ahIGNORE_AUTH` — authorization status does not gate a forced issuer
 *  reclaim.
 *
 *  `/*checkIssuer*‌/ false` is passed to `directSendNoFee` because MPT
 *  issuance ownership is already established by the issuance object itself
 *  (verified in preclaim); the IOU-specific issuer-involvement check in
 *  `directSendNoFee` is not applicable to the MPT model.
 *
 *  @param ctx Mutable apply context.
 *  @return The result of `directSendNoFee` — `tesSUCCESS` on success.
 */
template <>
TER
applyHelper<MPTIssue>(ApplyContext& ctx)
{
    AccountID const issuer = ctx.tx[sfAccount];
    auto clawAmount = ctx.tx[sfAmount];
    AccountID const holder = ctx.tx[sfHolder];

    // Re-query spendable balance at apply time with both freeze and auth
    // handling suppressed: authorization is irrelevant for a forced issuer
    // reclaim, and the issuer may have frozen the holder's token.
    STAmount const spendableAmount = accountHolds(
        ctx.view(),
        holder,
        clawAmount.get<MPTIssue>(),
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        ctx.journal);

    return directSendNoFee(
        ctx.view(),
        holder,
        issuer,
        std::min(spendableAmount, clawAmount),
        /*checkIssuer*/ false,
        ctx.journal);
}

/** Dispatch to the asset-type-specific apply helper and commit mutations.
 *
 *  Resolves the runtime `std::variant<Issue, MPTIssue>` in `sfAmount` to a
 *  compile-time template parameter and forwards to `applyHelper<Issue>` or
 *  `applyHelper<MPTIssue>`. The chosen helper transfers tokens from the
 *  holder to the issuer via `directSendNoFee`, capped at the holder's
 *  current spendable balance.
 *
 *  @return The result of the appropriate `applyHelper` specialisation.
 */
TER
Clawback::doApply()
{
    return std::visit(
        [&]<typename T>(T const&) { return applyHelper<T>(ctx_); },
        ctx_.tx[sfAmount].asset().value());
}

/** Per-SLE invariant visitor hook — currently a no-op.
 *
 *  Reserved for future transaction-specific invariant checks. The global
 *  `ValidClawback` invariant checker (in `InvariantCheck.cpp`) verifies that
 *  at most one trust line or MPT object is modified and that the holder's
 *  balance remains non-negative; no per-transactor check is needed here yet.
 */
void
Clawback::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** Invariant finalisation hook — currently a no-op that always passes.
 *
 *  Reserved for future transaction-specific invariant checks. Global invariant
 *  enforcement (e.g., `XRPNotCreated`, `ValidClawback`) is performed by the
 *  framework independently of this hook.
 *
 *  @return Always `true`; no violations are currently checked.
 */
bool
Clawback::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
