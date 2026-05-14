#include <xrpl/tx/transactors/token/TrustSet.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DelegateHelpers.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Permissions.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <memory>
#include <unordered_set>

namespace {

/** Apply normal-freeze and deep-freeze flag changes to a trust-line flag word.
 *
 *  This helper is the single authoritative place that translates the four
 *  transaction flags (`tfSetFreeze`, `tfClearFreeze`, `tfSetDeepFreeze`,
 *  `tfClearDeepFreeze`) into mutations of the per-side `lsfLow*`/`lsfHigh*`
 *  flag bits stored in the `RippleState` SLE.  It is called identically from
 *  both `preclaim` (to simulate the post-transaction flag state for invariant
 *  validation) and `doApply` (to compute the value actually written to the
 *  ledger).  Sharing a single implementation guarantees the two phases always
 *  agree on the resulting flag state, preventing silent divergence bugs.
 *
 *  Semantics:
 *  - Set-freeze wins over clear-freeze when both are present (the pair is
 *    treated as a no-op).  `bNoFreeze` additionally suppresses setting.
 *  - Deep-freeze follows the same mutual-exclusion rule independently of
 *    normal freeze, but `preclaim` enforces the invariant that deep-freeze
 *    requires normal freeze to be set.
 *
 *  @param uFlags          Current flag word from the `RippleState` SLE (or
 *                         zero when the line does not yet exist).
 *  @param bHigh           `true` if the transacting account is the high side
 *                         of the trust line (account ID > counterparty ID).
 *                         Selects `lsfHigh*` vs `lsfLow*` flag constants.
 *  @param bNoFreeze       `true` if the transacting account has `lsfNoFreeze`
 *                         set, permanently waiving freeze authority.
 *  @param bSetFreeze      `true` if `tfSetFreeze` is present in the tx flags.
 *  @param bClearFreeze    `true` if `tfClearFreeze` is present in the tx flags.
 *  @param bSetDeepFreeze  `true` if `tfSetDeepFreeze` is present in the tx flags.
 *  @param bClearDeepFreeze `true` if `tfClearDeepFreeze` is present in the tx flags.
 *  @return                Updated flag word with freeze bits adjusted.
 */
uint32_t
computeFreezeFlags(
    uint32_t uFlags,
    bool bHigh,
    bool bNoFreeze,
    bool bSetFreeze,
    bool bClearFreeze,
    bool bSetDeepFreeze,
    bool bClearDeepFreeze)
{
    if (bSetFreeze && !bClearFreeze && !bNoFreeze)
    {
        uFlags |= (bHigh ? xrpl::lsfHighFreeze : xrpl::lsfLowFreeze);
    }
    else if (bClearFreeze && !bSetFreeze)
    {
        uFlags &= ~(bHigh ? xrpl::lsfHighFreeze : xrpl::lsfLowFreeze);
    }
    if (bSetDeepFreeze && !bClearDeepFreeze && !bNoFreeze)
    {
        uFlags |= (bHigh ? xrpl::lsfHighDeepFreeze : xrpl::lsfLowDeepFreeze);
    }
    else if (bClearDeepFreeze && !bSetDeepFreeze)
    {
        uFlags &= ~(bHigh ? xrpl::lsfHighDeepFreeze : xrpl::lsfLowDeepFreeze);
    }

    return uFlags;
}

}  // namespace

namespace xrpl {

std::uint32_t
TrustSet::getFlagsMask(PreflightContext const& ctx)
{
    return tfTrustSetMask;
}

NotTEC
TrustSet::preflight(PreflightContext const& ctx)
{
    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uTxFlags = tx.getFlags();

    if (!ctx.rules.enabled(featureDeepFreeze))
    {
        // Even though the deep freeze flags are included in the
        // `tfTrustSetMask`, they are not valid if the amendment is not enabled.
        if ((uTxFlags & (tfSetDeepFreeze | tfClearDeepFreeze)) != 0u)
        {
            return temINVALID_FLAG;
        }
    }

    STAmount const saLimitAmount(tx.getFieldAmount(sfLimitAmount));

    if (!isLegalNet(saLimitAmount))
        return temBAD_AMOUNT;

    if (saLimitAmount.native())
    {
        JLOG(j.trace()) << "Malformed transaction: specifies native limit "
                        << saLimitAmount.getFullText();
        return temBAD_LIMIT;
    }

    if (badCurrency() == saLimitAmount.get<Issue>().currency)
    {
        JLOG(j.trace()) << "Malformed transaction: specifies XRP as IOU";
        return temBAD_CURRENCY;
    }

    if (saLimitAmount < beast::kZERO)
    {
        JLOG(j.trace()) << "Malformed transaction: Negative credit limit.";
        return temBAD_LIMIT;
    }

    // Check if destination makes sense.
    auto const& issuer = saLimitAmount.getIssuer();

    if (!issuer || issuer == noAccount())
    {
        JLOG(j.trace()) << "Malformed transaction: no destination account.";
        return temDST_NEEDED;
    }

    return tesSUCCESS;
}

NotTEC
TrustSet::checkPermission(ReadView const& view, STTx const& tx)
{
    auto const delegate = tx[~sfDelegate];
    if (!delegate)
        return tesSUCCESS;

    auto const delegateKey = keylet::delegate(tx[sfAccount], *delegate);
    auto const sle = view.read(delegateKey);

    if (!sle)
        return terNO_DELEGATE_PERMISSION;

    if (isTesSuccess(checkTxPermission(sle, tx)))
        return tesSUCCESS;

    std::uint32_t const txFlags = tx.getFlags();

    if ((txFlags & tfTrustSetPermissionMask) != 0u)
        return terNO_DELEGATE_PERMISSION;

    if (tx.isFieldPresent(sfQualityIn) || tx.isFieldPresent(sfQualityOut))
        return terNO_DELEGATE_PERMISSION;

    auto const saLimitAmount = tx.getFieldAmount(sfLimitAmount);
    auto const sleRippleState = view.read(
        keylet::line(
            tx[sfAccount], saLimitAmount.getIssuer(), saLimitAmount.get<Issue>().currency));

    // if the trustline does not exist, granular permissions are
    // not allowed to create trustline
    if (!sleRippleState)
        return terNO_DELEGATE_PERMISSION;

    std::unordered_set<GranularPermissionType> granularPermissions;
    loadGranularPermission(sle, ttTRUST_SET, granularPermissions);

    if (((txFlags & tfSetfAuth) != 0u) && !granularPermissions.contains(TrustlineAuthorize))
        return terNO_DELEGATE_PERMISSION;
    if (((txFlags & tfSetFreeze) != 0u) && !granularPermissions.contains(TrustlineFreeze))
        return terNO_DELEGATE_PERMISSION;
    if (((txFlags & tfClearFreeze) != 0u) && !granularPermissions.contains(TrustlineUnfreeze))
        return terNO_DELEGATE_PERMISSION;

    // updating LimitAmount is not allowed only with granular permissions,
    // unless there's a new granular permission for this in the future.
    auto const curLimit = tx[sfAccount] > saLimitAmount.getIssuer()
        ? sleRippleState->getFieldAmount(sfHighLimit)
        : sleRippleState->getFieldAmount(sfLowLimit);

    STAmount saLimitAllow = saLimitAmount;
    saLimitAllow.get<Issue>().account = tx[sfAccount];

    if (curLimit != saLimitAllow)
        return terNO_DELEGATE_PERMISSION;

    return tesSUCCESS;
}

/** Read-only ledger checks for `TrustSet`.
 *
 *  Implementation notes for each check (in execution order):
 *
 *  **`tfSetfAuth` guard**: the flag is meaningful only when the issuer account
 *  has `lsfRequireAuth` enabled; attempting it otherwise returns
 *  `tefNO_AUTH_REQUIRED` (no fee charged).
 *
 *  **`lsfDisallowIncomingTrustline` softening**: the original `featureDisallowIncoming`
 *  implementation blocked ALL trust-set operations when the destination had
 *  opted out, including modifications to lines that already existed.  The
 *  `fixDisallowIncomingV1` amendment corrects this by allowing the transaction
 *  to proceed when a line already exists between the two parties — only new
 *  line creation is gated by the opt-out preference.
 *
 *  **Pseudo-account allow-listing**: trust lines to pseudo-accounts are
 *  generally prohibited.  The block is not amendment-gated because the
 *  pseudo-account discriminator fields (`sfAMMID`, `sfVaultID`,
 *  `sfLoanBrokerID`) are only populated when the corresponding amendment is
 *  active, so the guard is implicitly gated.  Three narrow exceptions apply:
 *  - AMM pseudo-accounts: a new trust line is allowed only when the currency
 *    matches the pool's LP token and the AMM holds non-zero liquidity
 *    (`lpTokenBalance > 0`); modifying an existing line is always permitted.
 *  - Vault and loan-broker pseudo-accounts: only modification of an existing
 *    line passes; new line creation returns `tecNO_PERMISSION`.
 *  - Any other pseudo-account type returns `tecPSEUDO_ACCOUNT`.
 *
 *  **Deep-freeze invariant simulation**: when `featureDeepFreeze` is active,
 *  `computeFreezeFlags()` is called with the current flag word (zero if the
 *  line does not yet exist) to predict the post-transaction flag state.  The
 *  invariant `deepFrozen → frozen` is then checked on that simulated state.
 *  Rejecting the violation here — before any writes — is cheaper and keeps
 *  `doApply` free of conditional rollback logic.
 */
TER
TrustSet::preclaim(PreclaimContext const& ctx)
{
    auto const id = ctx.tx[sfAccount];

    auto const sle = ctx.view.read(keylet::account(id));
    if (!sle)
        return terNO_ACCOUNT;

    std::uint32_t const uTxFlags = ctx.tx.getFlags();

    bool const bSetAuth = (uTxFlags & tfSetfAuth) != 0u;

    if (bSetAuth && ((sle->getFieldU32(sfFlags) & lsfRequireAuth) == 0u))
    {
        JLOG(ctx.j.trace()) << "Retry: Auth not required.";
        return tefNO_AUTH_REQUIRED;
    }

    auto const saLimitAmount = ctx.tx[sfLimitAmount];

    auto const currency = saLimitAmount.get<Issue>().currency;
    auto const uDstAccountID = saLimitAmount.getIssuer();

    if (id == uDstAccountID)
        return temDST_IS_SRC;

    // This might be nullptr
    auto const sleDst = ctx.view.read(keylet::account(uDstAccountID));
    if ((ammEnabled(ctx.view.rules()) || ctx.view.rules().enabled(featureSingleAssetVault)) &&
        sleDst == nullptr)
        return tecNO_DST;

    // If the destination has opted to disallow incoming trustlines
    // then honour that flag
    if ((sleDst->getFlags() & lsfDisallowIncomingTrustline) != 0u)
    {
        // The original implementation of featureDisallowIncoming was
        // too restrictive. If
        //   o fixDisallowIncomingV1 is enabled and
        //   o The trust line already exists
        // Then allow the TrustSet.
        if (ctx.view.rules().enabled(fixDisallowIncomingV1) &&
            ctx.view.exists(keylet::line(id, uDstAccountID, currency)))
        {
            // pass
        }
        else
        {
            return tecNO_PERMISSION;
        }
    }

    // In general, trust lines to pseudo accounts are not permitted, unless
    // enabled in the code section below, for specific cases. This block is not
    // amendment-gated because sleDst will not have a pseudo-account designator
    // field populated, unless the appropriate amendment was already enabled.
    if (sleDst && isPseudoAccount(sleDst))
    {
        if (sleDst->isFieldPresent(sfAMMID))
        {
            if (ctx.view.exists(keylet::line(id, uDstAccountID, currency)))
            {
                // pass
            }
            else if (auto const ammSle = ctx.view.read({ltAMM, sleDst->getFieldH256(sfAMMID)}))
            {
                auto const lpTokens = ammSle->getFieldAmount(sfLPTokenBalance);
                if (lpTokens == beast::kZERO)
                {
                    return tecAMM_EMPTY;
                }
                if (lpTokens.get<Issue>().currency != saLimitAmount.get<Issue>().currency)
                {
                    return tecNO_PERMISSION;
                }
            }
            else
            {
                return tecINTERNAL;  // LCOV_EXCL_LINE
            }
        }
        else if (sleDst->isFieldPresent(sfVaultID) || sleDst->isFieldPresent(sfLoanBrokerID))
        {
            if (!ctx.view.exists(keylet::line(id, uDstAccountID, currency)))
                return tecNO_PERMISSION;
            // else pass
        }
        else
        {
            return tecPSEUDO_ACCOUNT;
        }
    }

    if (ctx.view.rules().enabled(featureDeepFreeze))
    {
        bool const bNoFreeze = sle->isFlag(lsfNoFreeze);
        bool const bSetFreeze = (uTxFlags & tfSetFreeze) != 0u;
        bool const bSetDeepFreeze = (uTxFlags & tfSetDeepFreeze) != 0u;

        if (bNoFreeze && (bSetFreeze || bSetDeepFreeze))
        {
            // Cannot freeze the trust line if NoFreeze is set
            return tecNO_PERMISSION;
        }

        bool const bClearFreeze = (uTxFlags & tfClearFreeze) != 0u;
        bool const bClearDeepFreeze = (uTxFlags & tfClearDeepFreeze) != 0u;
        if ((bSetFreeze || bSetDeepFreeze) && (bClearFreeze || bClearDeepFreeze))
        {
            // Freezing and unfreezing in the same transaction should be
            // illegal
            return tecNO_PERMISSION;
        }

        bool const bHigh = id > uDstAccountID;
        auto const sleRippleState = ctx.view.read(keylet::line(id, uDstAccountID, currency));
        std::uint32_t uFlags = sleRippleState ? sleRippleState->getFieldU32(sfFlags) : 0u;
        uFlags = computeFreezeFlags(
            uFlags, bHigh, bNoFreeze, bSetFreeze, bClearFreeze, bSetDeepFreeze, bClearDeepFreeze);

        auto const frozen = uFlags & (bHigh ? lsfHighFreeze : lsfLowFreeze);
        auto const deepFrozen = uFlags & (bHigh ? lsfHighDeepFreeze : lsfLowDeepFreeze);

        // Enforce: deepFrozen → frozen.  Covers both "set deep without
        // normal freeze" and "clear normal freeze while deep-frozen".
        if ((deepFrozen != 0u) && (frozen == 0u))
        {
            return tecNO_PERMISSION;
        }
    }

    return tesSUCCESS;
}

/** Apply the `TrustSet` transaction to the mutable ledger view.
 *
 *  **`bHigh` convention**: a `RippleState` SLE stores both sides of a trust
 *  relationship in one shared entry.  Per-side fields come in symmetric pairs
 *  (`sfLowLimit`/`sfHighLimit`, `sfLowQualityIn`/`sfHighQualityIn`,
 *  `lsfLowFreeze`/`lsfHighFreeze`, etc.).  `bHigh = (account_ > uDstAccountID)`
 *  selects the correct field of each pair throughout the function without
 *  duplicating logic.  Two concurrent `TrustSet` transactions by each party
 *  both write to the same SLE but modify disjoint fields.
 *
 *  **Reserve exemption for onboarding**: the incremental reserve for creating a
 *  new trust line is waived entirely when the submitter owns fewer than two
 *  objects (`uOwnerCount < 2`).  Without this exemption a gateway funding a
 *  brand-new user would need to deposit not just the base account reserve but
 *  also the trust-line reserve — surplus XRP the user could pocket without ever
 *  using the gateway.  The exemption caps the minimum viable onboarding cost at
 *  the account reserve alone.
 *
 *  **Quality normalization**: a quality value of exactly `QUALITY_ONE`
 *  (1 000 000 000) is the canonical "no adjustment" value and is stored as zero
 *  (field made absent via `makeFieldAbsent`).  `uQualityOut` is normalized to
 *  zero immediately after reading from the transaction; `uQualityIn` and the
 *  values read back from the existing line are normalized before the reserve
 *  decision is made.  This prevents callers from writing an explicit
 *  `QUALITY_ONE` and wasting 4 bytes of ledger storage per side.
 *
 *  **Reserve recomputation**: after updating all fields, the need for a reserve
 *  on each side is derived from scratch by testing whether any per-side state
 *  deviates from defaults (non-zero quality, non-zero limit, freeze flag, positive
 *  balance, or a `noRipple` preference that disagrees with the account's
 *  `lsfDefaultRipple`).  If the computed need differs from `lsfLowReserve`/
 *  `lsfHighReserve`, `adjustOwnerCount` is called with ±1 to keep the owner
 *  count accurate.
 *
 *  **Auto-deletion (`bDefault`)**: when both sides reach fully default state
 *  (`bLowReserveClear && bHighReserveClear`) or the currency is the
 *  `badCurrency()` sentinel, `trustDelete` removes the `RippleState` SLE from
 *  the ledger and both accounts' owner directories, preventing stale
 *  zero-balance objects from accumulating.
 *
 *  **Delegation to `RippleStateHelpers`**: `trustCreate` and `trustDelete`
 *  handle the low-level SLE construction, directory insertion/removal, and
 *  initial `adjustOwnerCount` for the creating account.  This keeps mutation
 *  logic centralised and reusable by other transactors that implicitly create
 *  trust lines (e.g., `issueIOU`).
 */
TER
TrustSet::doApply()
{
    TER terResult = tesSUCCESS;

    STAmount const saLimitAmount(ctx_.tx.getFieldAmount(sfLimitAmount));
    bool const bQualityIn(ctx_.tx.isFieldPresent(sfQualityIn));
    bool const bQualityOut(ctx_.tx.isFieldPresent(sfQualityOut));

    Currency const currency(saLimitAmount.get<Issue>().currency);
    AccountID const uDstAccountID(saLimitAmount.getIssuer());

    bool const bHigh = account_ > uDstAccountID;

    auto const sle = view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    std::uint32_t const uOwnerCount = sle->getFieldU32(sfOwnerCount);

    XRPAmount const reserveCreate(
        (uOwnerCount < 2) ? XRPAmount(beast::kZERO)
                          : view().fees().accountReserve(uOwnerCount + 1));

    std::uint32_t const uQualityIn(bQualityIn ? ctx_.tx.getFieldU32(sfQualityIn) : 0);
    std::uint32_t uQualityOut(bQualityOut ? ctx_.tx.getFieldU32(sfQualityOut) : 0);

    if (bQualityOut && QUALITY_ONE == uQualityOut)
        uQualityOut = 0;

    std::uint32_t const uTxFlags = ctx_.tx.getFlags();

    bool const bSetAuth = (uTxFlags & tfSetfAuth) != 0u;
    bool const bSetNoRipple = (uTxFlags & tfSetNoRipple) != 0u;
    bool const bClearNoRipple = (uTxFlags & tfClearNoRipple) != 0u;
    bool const bSetFreeze = (uTxFlags & tfSetFreeze) != 0u;
    bool const bClearFreeze = (uTxFlags & tfClearFreeze) != 0u;
    bool const bSetDeepFreeze = (uTxFlags & tfSetDeepFreeze) != 0u;
    bool const bClearDeepFreeze = (uTxFlags & tfClearDeepFreeze) != 0u;

    auto viewJ = ctx_.registry.get().getJournal("View");

    SLE::pointer const sleDst = view().peek(keylet::account(uDstAccountID));

    if (!sleDst)
    {
        JLOG(j_.trace()) << "Delay transaction: Destination account does not exist.";
        return tecNO_DST;
    }

    STAmount saLimitAllow = saLimitAmount;
    saLimitAllow.get<Issue>().account = account_;

    SLE::pointer const sleRippleState =
        view().peek(keylet::line(account_, uDstAccountID, currency));

    if (sleRippleState)
    {
        STAmount saLowBalance;
        STAmount saLowLimit;
        STAmount saHighBalance;
        STAmount saHighLimit;
        std::uint32_t uLowQualityIn = 0;
        std::uint32_t uLowQualityOut = 0;
        std::uint32_t uHighQualityIn = 0;
        std::uint32_t uHighQualityOut = 0;
        auto const& uLowAccountID = !bHigh ? account_ : uDstAccountID;
        auto const& uHighAccountID = bHigh ? account_ : uDstAccountID;
        SLE::ref sleLowAccount = !bHigh ? sle : sleDst;
        SLE::ref sleHighAccount = bHigh ? sle : sleDst;

        //
        // Balances
        //

        saLowBalance = sleRippleState->getFieldAmount(sfBalance);
        saHighBalance = -saLowBalance;

        //
        // Limits
        //

        sleRippleState->setFieldAmount(!bHigh ? sfLowLimit : sfHighLimit, saLimitAllow);

        saLowLimit = !bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfLowLimit);
        saHighLimit = bHigh ? saLimitAllow : sleRippleState->getFieldAmount(sfHighLimit);

        //
        // Quality in
        //

        if (!bQualityIn)
        {
            uLowQualityIn = sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = sleRippleState->getFieldU32(sfHighQualityIn);
        }
        else if (uQualityIn != 0u)
        {
            sleRippleState->setFieldU32(!bHigh ? sfLowQualityIn : sfHighQualityIn, uQualityIn);

            uLowQualityIn = !bHigh ? uQualityIn : sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = bHigh ? uQualityIn : sleRippleState->getFieldU32(sfHighQualityIn);
        }
        else
        {
            sleRippleState->makeFieldAbsent(!bHigh ? sfLowQualityIn : sfHighQualityIn);

            uLowQualityIn = !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityIn);
            uHighQualityIn = bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityIn);
        }

        if (QUALITY_ONE == uLowQualityIn)
            uLowQualityIn = 0;

        if (QUALITY_ONE == uHighQualityIn)
            uHighQualityIn = 0;

        //
        // Quality out
        //

        if (!bQualityOut)
        {
            uLowQualityOut = sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut = sleRippleState->getFieldU32(sfHighQualityOut);
        }
        else if (uQualityOut != 0u)
        {
            sleRippleState->setFieldU32(!bHigh ? sfLowQualityOut : sfHighQualityOut, uQualityOut);

            uLowQualityOut = !bHigh ? uQualityOut : sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut = bHigh ? uQualityOut : sleRippleState->getFieldU32(sfHighQualityOut);
        }
        else
        {
            sleRippleState->makeFieldAbsent(!bHigh ? sfLowQualityOut : sfHighQualityOut);

            uLowQualityOut = !bHigh ? 0 : sleRippleState->getFieldU32(sfLowQualityOut);
            uHighQualityOut = bHigh ? 0 : sleRippleState->getFieldU32(sfHighQualityOut);
        }

        std::uint32_t const uFlagsIn(sleRippleState->getFieldU32(sfFlags));
        std::uint32_t uFlagsOut(uFlagsIn);

        if (bSetNoRipple && !bClearNoRipple)
        {
            if ((bHigh ? saHighBalance : saLowBalance) >= beast::kZERO)
            {
                uFlagsOut |= (bHigh ? lsfHighNoRipple : lsfLowNoRipple);
            }
            else
            {
                // Cannot set noRipple on a negative balance.
                return tecNO_PERMISSION;
            }
        }
        else if (bClearNoRipple && !bSetNoRipple)
        {
            uFlagsOut &= ~(bHigh ? lsfHighNoRipple : lsfLowNoRipple);
        }

        // Have to use lsfNoFreeze to maintain pre-deep freeze behavior
        bool const bNoFreeze = sle->isFlag(lsfNoFreeze);
        uFlagsOut = computeFreezeFlags(
            uFlagsOut,
            bHigh,
            bNoFreeze,
            bSetFreeze,
            bClearFreeze,
            bSetDeepFreeze,
            bClearDeepFreeze);

        if (QUALITY_ONE == uLowQualityOut)
            uLowQualityOut = 0;

        if (QUALITY_ONE == uHighQualityOut)
            uHighQualityOut = 0;

        bool const bLowDefRipple = (sleLowAccount->getFlags() & lsfDefaultRipple) != 0u;
        bool const bHighDefRipple = (sleHighAccount->getFlags() & lsfDefaultRipple) != 0u;

        bool const bLowReserveSet = (uLowQualityIn != 0u) || (uLowQualityOut != 0u) ||
            ((uFlagsOut & lsfLowNoRipple) == 0) != bLowDefRipple ||
            ((uFlagsOut & lsfLowFreeze) != 0u) || saLowLimit || saLowBalance > beast::kZERO;
        bool const bLowReserveClear = !bLowReserveSet;

        bool const bHighReserveSet = (uHighQualityIn != 0u) || (uHighQualityOut != 0u) ||
            ((uFlagsOut & lsfHighNoRipple) == 0) != bHighDefRipple ||
            ((uFlagsOut & lsfHighFreeze) != 0u) || saHighLimit || saHighBalance > beast::kZERO;
        bool const bHighReserveClear = !bHighReserveSet;

        bool const bDefault = bLowReserveClear && bHighReserveClear;

        bool const bLowReserved = (uFlagsIn & lsfLowReserve) != 0u;
        bool const bHighReserved = (uFlagsIn & lsfHighReserve) != 0u;

        bool bReserveIncrease = false;

        if (bSetAuth)
        {
            uFlagsOut |= (bHigh ? lsfHighAuth : lsfLowAuth);
        }

        if (bLowReserveSet && !bLowReserved)
        {
            adjustOwnerCount(view(), sleLowAccount, 1, viewJ);
            uFlagsOut |= lsfLowReserve;

            if (!bHigh)
                bReserveIncrease = true;
        }

        if (bLowReserveClear && bLowReserved)
        {
            adjustOwnerCount(view(), sleLowAccount, -1, viewJ);
            uFlagsOut &= ~lsfLowReserve;
        }

        if (bHighReserveSet && !bHighReserved)
        {
            adjustOwnerCount(view(), sleHighAccount, 1, viewJ);
            uFlagsOut |= lsfHighReserve;

            if (bHigh)
                bReserveIncrease = true;
        }

        if (bHighReserveClear && bHighReserved)
        {
            adjustOwnerCount(view(), sleHighAccount, -1, viewJ);
            uFlagsOut &= ~lsfHighReserve;
        }

        if (uFlagsIn != uFlagsOut)
            sleRippleState->setFieldU32(sfFlags, uFlagsOut);

        if (bDefault || badCurrency() == currency)
        {
            terResult = trustDelete(view(), sleRippleState, uLowAccountID, uHighAccountID, viewJ);
        }
        // Reserve is not scaled by load.
        else if (bReserveIncrease && preFeeBalance_ < reserveCreate)
        {
            JLOG(j_.trace()) << "Delay transaction: Insufficent reserve to "
                                "add trust line.";

            // Another transaction could provide XRP to the account and then
            // this transaction would succeed.
            terResult = tecINSUF_RESERVE_LINE;
        }
        else
        {
            view().update(sleRippleState);

            JLOG(j_.trace()) << "Modify ripple line";
        }
    }
    else if (
        !saLimitAmount &&                         // Setting default limit.
        (!bQualityIn || (uQualityIn == 0u)) &&    // Not setting quality in or
                                                  // setting default quality in.
        (!bQualityOut || (uQualityOut == 0u)) &&  // Not setting quality out or
                                                  // setting default quality out.
        (!bSetAuth))
    {
        JLOG(j_.trace()) << "Redundant: Setting non-existent ripple line to defaults.";
        return tecNO_LINE_REDUNDANT;
    }
    else if (preFeeBalance_ < reserveCreate)  // Reserve is not scaled by
                                              // load.
    {
        JLOG(j_.trace()) << "Delay transaction: Line does not exist. "
                            "Insufficent reserve to create line.";

        // Another transaction could create the account and then this
        // transaction would succeed.
        terResult = tecNO_LINE_INSUF_RESERVE;
    }
    else
    {
        STAmount const saBalance(Issue{currency, noAccount()});

        auto const k = keylet::line(account_, uDstAccountID, currency);

        JLOG(j_.trace()) << "doTrustSet: Creating ripple line: " << to_string(k.key);

        terResult = trustCreate(
            view(),
            bHigh,
            account_,
            uDstAccountID,
            k.key,
            sle,
            bSetAuth,
            bSetNoRipple && !bClearNoRipple,
            bSetFreeze && !bClearFreeze,
            bSetDeepFreeze,
            saBalance,
            saLimitAllow,  // Limit for who is being charged.
            uQualityIn,
            uQualityOut,
            viewJ);
    }

    return terResult;
}

void
TrustSet::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
TrustSet::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
