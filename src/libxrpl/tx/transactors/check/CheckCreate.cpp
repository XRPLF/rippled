/** @file
 *  Implements `CheckCreate`, the first leg of the XRPL check lifecycle.
 *
 *  A Check ledger object authorizes a named destination to pull funds from
 *  the sender's account at a later time (via `CheckCash`) or cancel the
 *  authorization (via `CheckCancel`). No funds move at creation — the
 *  sender merely commits a maximum drawable amount (`sfSendMax`) denominated
 *  in XRP, an IOU, or (under `featureMPTokensV2`) an MPT.
 *
 *  This file contains all three pipeline phases for `ttCHECK_CREATE`:
 *  - `checkExtraFeatures` — amendment guard for MPT asset support
 *  - `preflight` — stateless structural field validation
 *  - `preclaim` — read-only ledger-state validation
 *  - `doApply` — atomic ledger mutation (SLE creation, directory entries,
 *    reserve accounting)
 *
 *  @see CheckCash.cpp, CheckCancel.cpp for the sibling transactors that
 *      operate on the Check SLE produced here.
 */
#include <xrpl/tx/transactors/check/CheckCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

/** Amendment guard for MPT-denominated checks.
 *
 *  Short-circuit gate: if `featureMPTokensV2` is enabled, any asset is
 *  allowed through. If not, an `MPTIssue` in `sfSendMax` causes immediate
 *  rejection. IOU- and XRP-denominated checks are unconditionally permitted
 *  regardless of amendment state. This isolates feature-flag logic at the
 *  boundary, keeping `preflight` free of amendment conditions.
 *
 *  @param ctx  The preflight context carrying the transaction and active
 *      rule set.
 *  @return `true` if the transaction may proceed to `preflight`; `false`
 *      if `sfSendMax` holds an MPT asset and `featureMPTokensV2` is inactive.
 */
bool
CheckCreate::checkExtraFeatures(xrpl::PreflightContext const& ctx)
{
    return ctx.rules.enabled(featureMPTokensV2) || !ctx.tx[sfSendMax].holds<MPTIssue>();
}

/** Stateless structural validation for `ttCHECK_CREATE`.
 *
 *  Validates transaction fields without consulting ledger state. Three
 *  invariants are enforced:
 *  1. The source and destination accounts must differ; a check written to
 *     oneself has no economic purpose (`temREDUNDANT`).
 *  2. `sfSendMax` must pass `isLegalNet()`, be strictly positive, and must
 *     not name `badAsset()` (a malformed currency code).
 *  3. If `sfExpiration` is present it must not be zero — zero would make the
 *     check immediately unspendable on creation.
 *
 *  @param ctx  The preflight context (no ledger access).
 *  @return `tesSUCCESS` if the transaction is structurally valid.
 *  @return `temREDUNDANT` if `sfAccount == sfDestination`.
 *  @return `temBAD_AMOUNT` if `sfSendMax` is zero, negative, or fails
 *      `isLegalNet()`.
 *  @return `temBAD_CURRENCY` if `sfSendMax` names `badAsset()`.
 *  @return `temBAD_EXPIRATION` if `sfExpiration` is present and equals zero.
 */
NotTEC
CheckCreate::preflight(PreflightContext const& ctx)
{
    if (ctx.tx[sfAccount] == ctx.tx[sfDestination])
    {
        JLOG(ctx.j.warn()) << "Malformed transaction: Check to self.";
        return temREDUNDANT;
    }

    {
        STAmount const sendMax{ctx.tx.getFieldAmount(sfSendMax)};
        if (!isLegalNet(sendMax) || sendMax.signum() <= 0)
        {
            JLOG(ctx.j.warn()) << "Malformed transaction: bad sendMax amount: "
                               << sendMax.getFullText();
            return temBAD_AMOUNT;
        }

        if (badAsset() == sendMax.asset())
        {
            JLOG(ctx.j.warn()) << "Malformed transaction: Bad currency.";
            return temBAD_CURRENCY;
        }
    }

    if (auto const optExpiry = ctx.tx[~sfExpiration])
    {
        if (*optExpiry == 0)
        {
            JLOG(ctx.j.warn()) << "Malformed transaction: bad expiration";
            return temBAD_EXPIRATION;
        }
    }

    return tesSUCCESS;
}

/** Read-only ledger-state validation for `ttCHECK_CREATE`.
 *
 *  Checks are validated in a deliberate order, each guarded by an early
 *  return. The full sequence is:
 *
 *  1. **Destination existence** — `tecNO_DST` if the account does not exist.
 *  2. **Incoming-check opt-out** — `tecNO_PERMISSION` if the destination has
 *     set `lsfDisallowIncomingCheck`.
 *  3. **Pseudo-account rejection** — `tecNO_PERMISSION` if the destination is
 *     a pseudo-account. Not amendment-gated: pseudo-account discriminator
 *     fields are themselves amendment-gated, so the behavior automatically
 *     tracks whatever amendments are active.
 *  4. **Destination tag** — `tecDST_TAG_NEEDED` when `lsfRequireDestTag` is
 *     set on the destination and the transaction omits `sfDestinationTag`.
 *  5. **Freeze / lock checks** (non-XRP assets only):
 *     - Global freeze → `tecFROZEN` (IOU) or `tecLOCKED` (MPT).
 *     - IOU: per-line freeze on the sender-to-issuer and issuer-to-destination
 *       trustlines, respecting `lsfHighFreeze`/`lsfLowFreeze` account
 *       ordering. A missing sender trustline is permitted — the check is
 *       speculative and the line need not exist at creation time.
 *     - MPT: individual holder-level freeze on sender and destination (skipped
 *       when either is the issuer) → `tecLOCKED`.
 *  6. **Pre-creation expiry** — `tecEXPIRED` if `sfExpiration` has already
 *     passed. Prevents creating a dead ledger entry.
 *  7. **Trade capability** — `canTrade()` rejects assets that have trading
 *     disabled at the MPT level.
 *
 *  @param ctx  The preclaim context providing a read-only ledger view.
 *  @return `tesSUCCESS` if all preconditions are met.
 *  @return `tecNO_DST` if the destination account does not exist.
 *  @return `tecNO_PERMISSION` if the destination has opted out of incoming
 *      checks or is a pseudo-account.
 *  @return `tecDST_TAG_NEEDED` if a destination tag is required but absent.
 *  @return `tecFROZEN` if an IOU trust line (global or per-line) is frozen.
 *  @return `tecLOCKED` if the MPT asset or an MPT holding is locked.
 *  @return `tecEXPIRED` if `sfExpiration` has already passed.
 */
TER
CheckCreate::preclaim(PreclaimContext const& ctx)
{
    AccountID const dstId{ctx.tx[sfDestination]};
    AccountID const srcId{ctx.tx[sfAccount]};
    auto const sleDst = ctx.view.read(keylet::account(dstId));
    if (!sleDst)
    {
        JLOG(ctx.j.warn()) << "Destination account does not exist.";
        return tecNO_DST;
    }

    auto const flags = sleDst->getFlags();

    if ((flags & lsfDisallowIncomingCheck) != 0u)
        return tecNO_PERMISSION;

    // Pseudo-accounts cannot cash checks. Not amendment-gated because all
    // writes to pseudo-account discriminator fields are themselves
    // amendment-gated, so this check always matches the active amendments.
    if (isPseudoAccount(sleDst))
        return tecNO_PERMISSION;

    if (((flags & lsfRequireDestTag) != 0u) && !ctx.tx.isFieldPresent(sfDestinationTag))
    {
        JLOG(ctx.j.warn()) << "Malformed transaction: DestinationTag required.";
        return tecDST_TAG_NEEDED;
    }

    {
        STAmount const sendMax{ctx.tx[sfSendMax]};
        if (!sendMax.native())
        {
            AccountID const& issuerId{sendMax.getIssuer()};
            if (isGlobalFrozen(ctx.view, sendMax.asset()))
            {
                JLOG(ctx.j.warn()) << "Creating a check for frozen asset";
                return sendMax.asset().holds<MPTIssue>() ? tecLOCKED : tecFROZEN;
            }
            auto const err = sendMax.asset().visit(
                [&](Issue const& issue) -> std::optional<TER> {
                    // A missing sender trustline is intentionally permitted —
                    // the check is speculative and the line need not exist yet.
                    if (issuerId != srcId)
                    {
                        auto const sleTrust =
                            ctx.view.read(keylet::line(srcId, issuerId, issue.currency));
                        if (sleTrust &&
                            sleTrust->isFlag((issuerId > srcId) ? lsfHighFreeze : lsfLowFreeze))
                        {
                            JLOG(ctx.j.warn()) << "Creating a check for frozen trustline.";
                            return tecFROZEN;
                        }
                    }
                    if (issuerId != dstId)
                    {
                        auto const sleTrust =
                            ctx.view.read(keylet::line(issuerId, dstId, issue.currency));
                        if (sleTrust &&
                            sleTrust->isFlag((dstId > issuerId) ? lsfHighFreeze : lsfLowFreeze))
                        {
                            JLOG(ctx.j.warn()) << "Creating a check for "
                                                  "destination frozen trustline.";
                            return tecFROZEN;
                        }
                    }

                    return std::nullopt;
                },
                [&](MPTIssue const& issue) -> std::optional<TER> {
                    if (srcId != issuerId && isFrozen(ctx.view, srcId, issue))
                        return tecLOCKED;
                    if (dstId != issuerId && isFrozen(ctx.view, dstId, issue))
                        return tecLOCKED;

                    return std::nullopt;
                });
            if (err)
                return *err;
        }
    }
    if (hasExpired(ctx.view, ctx.tx[~sfExpiration]))
    {
        JLOG(ctx.j.warn()) << "Creating a check that has already expired.";
        return tecEXPIRED;
    }

    return canTrade(ctx.view, ctx.tx[sfSendMax].asset());
}

/** Commits the Check creation to the ledger.
 *
 *  Executes the following mutations atomically (all-or-nothing under the
 *  `tec*`-rollback semantics of the transactor pipeline):
 *
 *  1. **Reserve check** against `preFeeBalance_` (the sender's balance
 *     captured before the transaction fee was deducted). Using the pre-fee
 *     balance deliberately allows accounts near the reserve floor to pay the
 *     fee and still create the check, rather than being locked out entirely.
 *
 *  2. **SLE construction** keyed by `keylet::check(account_, seq)` where
 *     `seq` is the transaction's sequence or ticket value. Using the sequence
 *     value as the keylet makes the check's ledger key deterministically
 *     computable from information the recipient already knows. Optional fields
 *     (`sfSourceTag`, `sfDestinationTag`, `sfInvoiceID`, `sfExpiration`) are
 *     written only when present in the transaction.
 *
 *  3. **Dual directory insertion** into both the destination's and the
 *     sender's owner directories. The resulting page numbers are stored as
 *     `sfDestinationNode` and `sfOwnerNode` on the SLE, enabling O(1)
 *     removal by `CheckCash` and `CheckCancel` via `dirRemove()`.
 *
 *  4. **Owner-count increment** via `adjustOwnerCount(view(), sle, 1, viewJ)`,
 *     raising the sender's reserve requirement by one increment.
 *
 *  @return `tesSUCCESS` on successful Check creation.
 *  @return `tecINSUFFICIENT_RESERVE` if the sender's pre-fee balance cannot
 *      cover the reserve requirement for `ownerCount + 1`.
 *  @return `tecDIR_FULL` if either owner directory has no room for a new
 *      entry (ledger-corruption sentinel, `LCOV_EXCL`).
 *  @return `tefINTERNAL` if the sender's `AccountRoot` SLE cannot be peeked
 *      (ledger-corruption sentinel, `LCOV_EXCL`).
 */
TER
CheckCreate::doApply()
{
    auto const sle = view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    {
        STAmount const reserve{view().fees().accountReserve(sle->getFieldU32(sfOwnerCount) + 1)};

        if (preFeeBalance_ < reserve)
            return tecINSUFFICIENT_RESERVE;
    }

    std::uint32_t const seq = ctx_.tx.getSeqValue();
    Keylet const checkKeylet = keylet::check(account_, seq);
    auto sleCheck = std::make_shared<SLE>(checkKeylet);

    sleCheck->setAccountID(sfAccount, account_);
    AccountID const dstAccountId = ctx_.tx[sfDestination];
    sleCheck->setAccountID(sfDestination, dstAccountId);
    sleCheck->setFieldU32(sfSequence, seq);
    sleCheck->setFieldAmount(sfSendMax, ctx_.tx[sfSendMax]);
    if (auto const srcTag = ctx_.tx[~sfSourceTag])
        sleCheck->setFieldU32(sfSourceTag, *srcTag);
    if (auto const dstTag = ctx_.tx[~sfDestinationTag])
        sleCheck->setFieldU32(sfDestinationTag, *dstTag);
    if (auto const invoiceId = ctx_.tx[~sfInvoiceID])
        sleCheck->setFieldH256(sfInvoiceID, *invoiceId);
    if (auto const expiry = ctx_.tx[~sfExpiration])
        sleCheck->setFieldU32(sfExpiration, *expiry);

    view().insert(sleCheck);

    auto viewJ = ctx_.registry.get().getJournal("View");
    if (dstAccountId != account_)
    {
        auto const page = view().dirInsert(
            keylet::ownerDir(dstAccountId), checkKeylet, describeOwnerDir(dstAccountId));

        JLOG(j_.trace()) << "Adding Check to destination directory " << to_string(checkKeylet.key)
                         << ": " << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        sleCheck->setFieldU64(sfDestinationNode, *page);
    }

    {
        auto const page =
            view().dirInsert(keylet::ownerDir(account_), checkKeylet, describeOwnerDir(account_));

        JLOG(j_.trace()) << "Adding Check to owner directory " << to_string(checkKeylet.key) << ": "
                         << (page ? "success" : "failure");

        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE

        sleCheck->setFieldU64(sfOwnerNode, *page);
    }

    adjustOwnerCount(view(), sle, 1, viewJ);
    return tesSUCCESS;
}

/** Per-entry invariant visitor hook (no-op stub).
 *
 *  No transaction-specific invariants are currently registered for
 *  `CheckCreate`. Reserved for future use.
 *
 *  @param  (unnamed)  Whether the entry is being deleted.
 *  @param  (unnamed)  SLE state before the transaction.
 *  @param  (unnamed)  SLE state after the transaction.
 */
void
CheckCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

/** Post-transaction invariant finalizer (no-op stub).
 *
 *  No transaction-specific invariants are currently registered for
 *  `CheckCreate`. Always returns `true`. Reserved for future use.
 *
 *  @return `true` unconditionally.
 */
bool
CheckCreate::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
