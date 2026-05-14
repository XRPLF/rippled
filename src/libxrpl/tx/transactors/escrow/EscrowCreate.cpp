/** @file
 *  Implements the EscrowCreate transactor, which locks XRP or token (IOU/MPT)
 *  funds in a conditional escrow ledger object.  The escrow can later be
 *  released by `EscrowFinish` or reclaimed by `EscrowCancel`.
 *
 *  Locked funds are completely unavailable to either party until one of those
 *  outcomes occurs.  Time-based unlocks and crypto-conditions are both
 *  supported unlock mechanisms.  Escrows may also carry an expiry after which
 *  only `EscrowCancel` is permitted.
 *
 *  Token escrow (`Issue`/`MPTIssue`) is gated on `featureTokenEscrow`.
 *
 *  @see https://xrpl.org/escrowcreate.html
 *  @see https://xrpl.org/escrowfinish.html
 *  @see https://xrpl.org/escrowcancel.html
 *  @see EscrowFinish
 *  @see EscrowCancel
 */

#include <xrpl/tx/transactors/escrow/EscrowCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/conditions/Condition.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
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
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <memory>
#include <system_error>
#include <variant>

namespace xrpl {

//------------------------------------------------------------------------------

TxConsequences
EscrowCreate::makeTxConsequences(PreflightContext const& ctx)
{
    auto const amount = ctx.tx[sfAmount];
    return TxConsequences{ctx.tx, isXRP(amount) ? amount.xrp() : beast::kZERO};
}

/** Stateless preflight checks specific to each non-XRP asset type.
 *
 *  Specialised for `Issue` (IOU) and `MPTIssue` (MPT).  Called from
 *  `EscrowCreate::preflight` after `featureTokenEscrow` has been confirmed
 *  active.  Validates amount polarity, magnitude, and asset-type constraints
 *  without touching the ledger.
 *
 *  @tparam T  `Issue` or `MPTIssue`.
 *  @param ctx stateless preflight context.
 *  @return    `tesSUCCESS`, or a `tem*` error code.
 */
template <ValidIssueType T>
static NotTEC
escrowCreatePreflightHelper(PreflightContext const& ctx);

/** IOU-specific preflight helper.
 *
 *  Rejects native (XRP) amounts, non-positive values, and the bad-currency
 *  sentinel.  The native guard prevents XRP amounts from reaching this path
 *  via an `Issue`-typed specialisation.
 *
 *  @param ctx stateless preflight context.
 *  @return    `temBAD_AMOUNT` for invalid amounts; `tesSUCCESS` otherwise.
 */
template <>
NotTEC
escrowCreatePreflightHelper<Issue>(PreflightContext const& ctx)
{
    STAmount const amount = ctx.tx[sfAmount];
    if (amount.native() || amount <= beast::kZERO)
        return temBAD_AMOUNT;

    if (badCurrency() == amount.get<Issue>().currency)
        return temBAD_CURRENCY;

    return tesSUCCESS;
}

/** MPT-specific preflight helper.
 *
 *  In addition to the checks common to all non-XRP assets (non-native, positive
 *  value), also requires `featureMPTokensV1` to be active and enforces the
 *  `kMAX_MP_TOKEN_AMOUNT` ceiling on the raw MPT balance.
 *
 *  @param ctx stateless preflight context.
 *  @return    `temDISABLED` if `featureMPTokensV1` is inactive;
 *             `temBAD_AMOUNT` for out-of-range or non-positive amounts;
 *             `tesSUCCESS` otherwise.
 */
template <>
NotTEC
escrowCreatePreflightHelper<MPTIssue>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    auto const amount = ctx.tx[sfAmount];
    if (amount.native() || amount.mpt() > MPTAmount{kMAX_MP_TOKEN_AMOUNT} || amount <= beast::kZERO)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

/** @copydoc EscrowCreate::preflight */
NotTEC
EscrowCreate::preflight(PreflightContext const& ctx)
{
    STAmount const amount{ctx.tx[sfAmount]};
    if (!isXRP(amount))
    {
        if (!ctx.rules.enabled(featureTokenEscrow))
            return temBAD_AMOUNT;

        if (auto const ret = std::visit(
                [&]<typename T>(T const&) { return escrowCreatePreflightHelper<T>(ctx); },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }
    else
    {
        if (amount <= beast::kZERO)
            return temBAD_AMOUNT;
    }

    // At least one of sfCancelAfter or sfFinishAfter must be present;
    // an escrow with neither has no defined lifecycle.
    if (!ctx.tx[~sfCancelAfter] && !ctx.tx[~sfFinishAfter])
        return temBAD_EXPIRATION;

    if (ctx.tx[~sfCancelAfter] && ctx.tx[~sfFinishAfter] &&
        ctx.tx[sfCancelAfter] <= ctx.tx[sfFinishAfter])
        return temBAD_EXPIRATION;

    // Without sfFinishAfter the escrow could be completed immediately on
    // creation, which is almost certainly a mistake. Require sfCondition
    // as an explicit unlock mechanism when no finish time is given.
    if (!ctx.tx[~sfFinishAfter] && !ctx.tx[~sfCondition])
        return temMALFORMED;

    if (auto const cb = ctx.tx[~sfCondition])
    {
        using namespace xrpl::cryptoconditions;

        std::error_code ec;

        auto condition = Condition::deserialize(*cb, ec);
        if (!condition)
        {
            JLOG(ctx.j.debug()) << "Malformed condition during escrow creation: " << ec.message();
            return temMALFORMED;
        }
    }

    return tesSUCCESS;
}

/** Read-only ledger checks specific to each non-XRP asset type during preclaim.
 *
 *  Specialised for `Issue` (IOU) and `MPTIssue` (MPT).  Called from
 *  `EscrowCreate::preclaim` after confirming `featureTokenEscrow` is active.
 *  Validates issuer permissions, sender/destination authorisation, freeze
 *  status, and spendable balance against the live ledger state.
 *
 *  @tparam T       `Issue` or `MPTIssue`.
 *  @param ctx      read-only preclaim context.
 *  @param account  sender's `AccountID`.
 *  @param dest     destination's `AccountID`.
 *  @param amount   escrowed amount.
 *  @return         `tesSUCCESS`, or a `tec*` error code.
 */
template <ValidIssueType T>
static TER
escrowCreatePreclaimHelper(
    PreclaimContext const& ctx,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount);

/** IOU-specific preclaim helper.
 *
 *  Enforces the following in order:
 *  - Issuer and sender must differ (no self-escrow of issued credit).
 *  - Issuer must have `lsfAllowTrustLineLocking` set — token escrow is
 *    opt-in for issuers.
 *  - A trust line between sender and issuer must exist.
 *  - Trust line balance polarity must match the XRP Ledger address-ordering
 *    convention (positive balance → issuer > account; negative → issuer <
 *    account).
 *  - Both sender and destination must satisfy `requireAuth` when the issuer
 *    mandates authorisation.
 *  - Neither the sender's nor the destination's trust line may be frozen.
 *  - Sender's spendable balance (checked with `fhIGNORE_FREEZE` to obtain the
 *    raw balance) must be positive and cover `amount`.
 *  - `canAdd` precision guard: the amount must be addable back to the balance
 *    without precision loss when the escrow later finishes.
 *
 *  @param ctx     read-only preclaim context.
 *  @param account sender's `AccountID`.
 *  @param dest    destination's `AccountID`.
 *  @param amount  escrowed IOU amount.
 *  @return        `tecNO_PERMISSION` if issuer == account or flags/auth fail;
 *                 `tecNO_ISSUER` if the issuer account does not exist;
 *                 `tecNO_LINE` if no trust line exists; `tecFROZEN` if either
 *                 party is frozen; `tecINSUFFICIENT_FUNDS` if balance is
 *                 insufficient; `tecPRECISION_LOSS` on arithmetic edge cases;
 *                 `tesSUCCESS` otherwise.
 */
template <>
TER
escrowCreatePreclaimHelper<Issue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount)
{
    Issue const& issue = amount.get<Issue>();
    AccountID const& issuer = amount.getIssuer();
    if (issuer == account)
        return tecNO_PERMISSION;

    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    if (!sleIssuer)
        return tecNO_ISSUER;
    if (!sleIssuer->isFlag(lsfAllowTrustLineLocking))
        return tecNO_PERMISSION;

    auto const sleRippleState = ctx.view.read(keylet::line(account, issuer, issue.currency));
    if (!sleRippleState)
        return tecNO_LINE;

    STAmount const balance = (*sleRippleState)[sfBalance];

    // Trust line balance polarity encodes address ordering: positive balance
    // means issuer has the higher address; negative means issuer has the lower.
    if (balance > beast::kZERO && issuer < account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    if (balance < beast::kZERO && issuer > account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    if (auto const ter = requireAuth(ctx.view, issue, account); !isTesSuccess(ter))
        return ter;

    if (auto const ter = requireAuth(ctx.view, issue, dest); !isTesSuccess(ter))
        return ter;

    if (isFrozen(ctx.view, account, issue))
        return tecFROZEN;

    if (isFrozen(ctx.view, dest, issue))
        return tecFROZEN;

    STAmount const spendableAmount = accountHolds(
        ctx.view, account, issue.currency, issuer, FreezeHandling::IgnoreFreeze, ctx.j);

    if (spendableAmount <= beast::kZERO)
        return tecINSUFFICIENT_FUNDS;

    if (spendableAmount < amount)
        return tecINSUFFICIENT_FUNDS;

    // Guard against precision loss that would occur when the locked amount is
    // returned to the sender's trust line balance on finish.
    if (!canAdd(spendableAmount, amount))
        return tecPRECISION_LOSS;

    return tesSUCCESS;
}

/** MPT-specific preclaim helper.
 *
 *  Mirrors the IOU helper in structure but applies MPT-specific rules:
 *  - Issuer and sender must differ.
 *  - The `MPTokenIssuance` object must exist and carry `lsfMPTCanEscrow`.
 *  - The sender must hold an `MPToken` object for this issuance.
 *  - Both sender and destination must pass `requireAuth` (WeakAuth) when the
 *    issuance mandates authorisation.
 *  - Lock/freeze status is checked with `isFrozen`; violations return
 *    `tecLOCKED` (not `tecFROZEN`) — the deliberate distinction reflects the
 *    different freeze semantics for MPTs vs. IOU trust lines.
 *  - Transfer eligibility is confirmed via `canTransfer`.
 *  - Sender's spendable MPT balance must be positive and cover `amount`.
 *
 *  @note Unlike the IOU path there is no `canAdd` precision check here because
 *      MPT arithmetic uses integer semantics without IOU rounding concerns.
 *
 *  @param ctx     read-only preclaim context.
 *  @param account sender's `AccountID`.
 *  @param dest    destination's `AccountID`.
 *  @param amount  escrowed MPT amount.
 *  @return        `tecNO_PERMISSION` if issuer == account or flags/auth fail;
 *                 `tecOBJECT_NOT_FOUND` if the issuance or sender's `MPToken`
 *                 is absent; `tecLOCKED` if either party is frozen/locked;
 *                 `tecINSUFFICIENT_FUNDS` if balance is insufficient;
 *                 `tesSUCCESS` otherwise.
 */
template <>
TER
escrowCreatePreclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID const issuer = amount.getIssuer();
    if (issuer == account)
        return tecNO_PERMISSION;

    auto const issuanceKey = keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    if (!sleIssuance->isFlag(lsfMPTCanEscrow))
        return tecNO_PERMISSION;

    if (sleIssuance->getAccountID(sfIssuer) != issuer)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    if (!ctx.view.exists(keylet::mptoken(issuanceKey.key, account)))
        return tecOBJECT_NOT_FOUND;

    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter = requireAuth(ctx.view, mptIssue, account, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    if (auto const ter = requireAuth(ctx.view, mptIssue, dest, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    // MPT freeze violations use tecLOCKED rather than tecFROZEN to distinguish
    // MPT lock semantics from IOU global/per-line freeze semantics.
    if (isFrozen(ctx.view, account, mptIssue))
        return tecLOCKED;

    if (isFrozen(ctx.view, dest, mptIssue))
        return tecLOCKED;

    if (auto const ter = canTransfer(ctx.view, mptIssue, account, dest); !isTesSuccess(ter))
        return ter;

    STAmount const spendableAmount = accountHolds(
        ctx.view,
        account,
        amount.get<MPTIssue>(),
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        ctx.j);

    if (spendableAmount <= beast::kZERO)
        return tecINSUFFICIENT_FUNDS;

    if (spendableAmount < amount)
        return tecINSUFFICIENT_FUNDS;

    return tesSUCCESS;
}

/** @copydoc EscrowCreate::preclaim */
TER
EscrowCreate::preclaim(PreclaimContext const& ctx)
{
    STAmount const amount{ctx.tx[sfAmount]};
    AccountID const account{ctx.tx[sfAccount]};
    AccountID const dest{ctx.tx[sfDestination]};

    auto const sled = ctx.view.read(keylet::account(dest));
    if (!sled)
        return tecNO_DST;

    // Pseudo-accounts cannot receive escrow. Note, this is not amendment-gated
    // because all writes to pseudo-account discriminator fields **are**
    // amendment gated, hence the behaviour of this check will always match the
    // currently active amendments.
    if (isPseudoAccount(sled))
        return tecNO_PERMISSION;

    if (!isXRP(amount))
    {
        if (!ctx.view.rules().enabled(featureTokenEscrow))
            return temDISABLED;  // LCOV_EXCL_LINE

        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowCreatePreclaimHelper<T>(ctx, account, dest, amount);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }
    return tesSUCCESS;
}

/** Debit the sender's token balance when creating a non-XRP escrow.
 *
 *  Specialised for `Issue` (IOU) and `MPTIssue` (MPT).  Performs the
 *  asset-type-specific ledger mutation that removes funds from the sender's
 *  spendable balance and places them under escrow control.
 *
 *  @tparam T        `Issue` or `MPTIssue`.
 *  @param view      mutable apply view.
 *  @param issuer    token issuer's `AccountID`.
 *  @param sender    sender's `AccountID`.
 *  @param amount    escrowed amount.
 *  @param journal   logging journal.
 *  @return          `tesSUCCESS`, `tecINTERNAL` (defensive; should never fire),
 *                   or any error propagated from the underlying transfer helper.
 */
template <ValidIssueType T>
static TER
escrowLockApplyHelper(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal);

/** IOU locking: transfer tokens fee-free from sender back to issuer.
 *
 *  Conceptually retires the tokens from circulation; they will be re-issued
 *  to the destination by `EscrowFinish`.  `directSendNoFee` is used so no
 *  transfer fee is charged at lock time (the fee, if any, is charged on
 *  finish using the rate snapshotted in `sfTransferRate`).
 *
 *  @param view    mutable apply view.
 *  @param issuer  token issuer's `AccountID`.
 *  @param sender  sender's `AccountID`.
 *  @param amount  escrowed IOU amount.
 *  @param journal logging journal.
 *  @return        `tecINTERNAL` if issuer == sender (defensive, should never
 *                 fire in production); otherwise the result of
 *                 `directSendNoFee`.
 */
template <>
TER
escrowLockApplyHelper<Issue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal)
{
    // Defensive: preclaim already rejected issuer == sender; tecINTERNAL
    // here indicates an internal inconsistency rather than a user error.
    if (issuer == sender)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ter =
        directSendNoFee(view, sender, issuer, amount, !amount.holds<MPTIssue>(), journal);
    if (!isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

/** MPT locking: atomically move tokens into the sender's locked-amount field.
 *
 *  `lockEscrowMPT` decrements `sfMPTAmount` and increments `sfLockedAmount`
 *  on the sender's `MPToken` SLE, and increments `sfLockedAmount` on the
 *  `MPTokenIssuance` SLE.  Ownership of the tokens does not change — no
 *  issuer directory entry is needed because the issuance object tracks the
 *  total locked supply directly.
 *
 *  @param view    mutable apply view.
 *  @param issuer  token issuer's `AccountID`.
 *  @param sender  sender's `AccountID`.
 *  @param amount  escrowed MPT amount.
 *  @param journal logging journal.
 *  @return        `tecINTERNAL` if issuer == sender (defensive); otherwise
 *                 the result of `lockEscrowMPT`.
 */
template <>
TER
escrowLockApplyHelper<MPTIssue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal)
{
    // Defensive: see escrowLockApplyHelper<Issue> comment above.
    if (issuer == sender)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ter = lockEscrowMPT(view, sender, amount, journal);
    if (!isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

/** @copydoc EscrowCreate::doApply */
TER
EscrowCreate::doApply()
{
    auto const closeTime = ctx_.view().header().parentCloseTime;

    // Re-check time bounds against the current ledger's parentCloseTime.
    // A transaction that was valid at preflight may arrive in a ledger where
    // the expiry has already passed; creating an already-expired escrow must
    // be rejected here rather than persisted.
    if (ctx_.tx[~sfCancelAfter] && after(closeTime, ctx_.tx[sfCancelAfter]))
        return tecNO_PERMISSION;

    if (ctx_.tx[~sfFinishAfter] && after(closeTime, ctx_.tx[sfFinishAfter]))
        return tecNO_PERMISSION;

    auto const sle = ctx_.view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    STAmount const amount{ctx_.tx[sfAmount]};

    // Reserve check: the new escrow SLE adds one to the owner count.
    auto const reserve = ctx_.view().fees().accountReserve((*sle)[sfOwnerCount] + 1);

    auto const balance = sle->getFieldAmount(sfBalance).xrp();
    if (balance < reserve)
        return tecINSUFFICIENT_RESERVE;

    // For XRP escrows the sender's balance must also cover the locked principal.
    if (isXRP(amount))
    {
        if (balance < reserve + STAmount(amount).xrp())
            return tecUNFUNDED;
    }

    {
        auto const sled = ctx_.view().read(keylet::account(ctx_.tx[sfDestination]));
        if (!sled)
            return tecNO_DST;  // LCOV_EXCL_LINE
        if ((((*sled)[sfFlags] & lsfRequireDestTag) != 0u) && !ctx_.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;
    }

    // The escrow keylet is derived from the sender's account and the sequence
    // (or ticket) value; see SeqProxy.h for the rationale.
    Keylet const escrowKeylet = keylet::escrow(account_, ctx_.tx.getSeqValue());
    auto const slep = std::make_shared<SLE>(escrowKeylet);
    (*slep)[sfAmount] = amount;
    (*slep)[sfAccount] = account_;
    (*slep)[~sfCondition] = ctx_.tx[~sfCondition];
    (*slep)[~sfSourceTag] = ctx_.tx[~sfSourceTag];
    (*slep)[sfDestination] = ctx_.tx[sfDestination];
    (*slep)[~sfCancelAfter] = ctx_.tx[~sfCancelAfter];
    (*slep)[~sfFinishAfter] = ctx_.tx[~sfFinishAfter];
    (*slep)[~sfDestinationTag] = ctx_.tx[~sfDestinationTag];

    // fixIncludeKeyletFields: store sfSequence in the SLE so that
    // EscrowFinish/EscrowCancel can reconstruct the keylet without external
    // input, enabling more robust cross-ledger object navigation.
    if (ctx_.view().rules().enabled(fixIncludeKeyletFields))
    {
        (*slep)[sfSequence] = ctx_.tx.getSeqValue();
    }

    // Snapshot the issuer's transfer rate at creation time. EscrowFinish reads
    // this stored rate to compute the correct delivery amount; the issuer cannot
    // retroactively change the rate that applies to the locked funds.
    if (ctx_.view().rules().enabled(featureTokenEscrow) && !isXRP(amount))
    {
        auto const xferRate = transferRate(ctx_.view(), amount);
        if (xferRate != kPARITY_RATE)
            (*slep)[sfTransferRate] = xferRate.value;
    }

    ctx_.view().insert(slep);

    // Register the escrow in the sender's owner directory unconditionally.
    {
        auto page = ctx_.view().dirInsert(
            keylet::ownerDir(account_), escrowKeylet, describeOwnerDir(account_));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*slep)[sfOwnerNode] = *page;
    }

    // Register in the destination's owner directory for non-self-escrows.
    AccountID const dest = ctx_.tx[sfDestination];
    if (dest != account_)
    {
        auto page =
            ctx_.view().dirInsert(keylet::ownerDir(dest), escrowKeylet, describeOwnerDir(dest));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*slep)[sfDestinationNode] = *page;
    }

    // IOU escrow objects are added to the issuer's owner directory to help
    // track the total locked balance. For MPT, this isn't necessary because the
    // locked balance is already stored directly in the MPTokenIssuance object.
    AccountID const issuer = amount.getIssuer();
    if (!isXRP(amount) && issuer != account_ && issuer != dest && !amount.holds<MPTIssue>())
    {
        auto page =
            ctx_.view().dirInsert(keylet::ownerDir(issuer), escrowKeylet, describeOwnerDir(issuer));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*slep)[sfIssuerNode] = *page;
    }

    // Debit the sender. XRP is deducted directly from sfBalance; token amounts
    // are handled by asset-type-specific helpers via std::visit.
    if (isXRP(amount))
    {
        (*sle)[sfBalance] = (*sle)[sfBalance] - amount;
    }
    else
    {
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowLockApplyHelper<T>(ctx_.view(), issuer, account_, amount, j_);
                },
                amount.asset().value());
            !isTesSuccess(ret))
        {
            return ret;  // LCOV_EXCL_LINE
        }
    }

    adjustOwnerCount(ctx_.view(), sle, 1, ctx_.journal);
    ctx_.view().update(sle);
    return tesSUCCESS;
}

void
EscrowCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
EscrowCreate::finalizeInvariants(
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
