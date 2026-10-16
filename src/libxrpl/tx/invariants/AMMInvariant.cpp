#include <xrpl/tx/invariants/AMMInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>

#include <string>

namespace xrpl {

void
ValidAMM::visitEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after)
{
    if (isDelete)
    {
        if (before && before->getType() == ltAMM)
        {
            ammDeleted_ = true;
            lptAMMBalanceBeforeDeletion_ = before->getFieldAmount(sfLPTokenBalance);
        }
        return;
    }

    if (after)
    {
        auto const type = after->getType();
        // AMM object changed
        if (type == ltAMM)
        {
            ammAccount_ = after->getAccountID(sfAccount);
            lptAMMBalanceAfter_ = after->getFieldAmount(sfLPTokenBalance);
            ammSle_ = after;
            if (after->isFieldPresent(sfFeeGrowthGlobal0))
                feeGrowthGlobal0After_ =
                    Number{after->getFieldNumber(sfFeeGrowthGlobal0)};
            if (after->isFieldPresent(sfFeeGrowthGlobal1))
                feeGrowthGlobal1After_ =
                    Number{after->getFieldNumber(sfFeeGrowthGlobal1)};
        }
        // AMM pool changed
        else if (
            (type == ltRIPPLE_STATE && after->isFlag(lsfAMMNode)) ||
            (type == ltACCOUNT_ROOT && after->isFieldPresent(sfAMMID)) ||
            (type == ltMPTOKEN && after->isFlag(lsfMPTAMM)))
        {
            ammPoolChanged_ = true;
        }
    }

    if (before)
    {
        // AMM object changed
        if (before->getType() == ltAMM)
        {
            lptAMMBalanceBefore_ = before->getFieldAmount(sfLPTokenBalance);
            if (before->isFieldPresent(sfFeeGrowthGlobal0))
                feeGrowthGlobal0Before_ =
                    Number{before->getFieldNumber(sfFeeGrowthGlobal0)};
            if (before->isFieldPresent(sfFeeGrowthGlobal1))
                feeGrowthGlobal1Before_ =
                    Number{before->getFieldNumber(sfFeeGrowthGlobal1)};
        }
    }

    // Tick lifecycle tracking for the bitmap consistency invariant.
    // We only care about tick-SLE creation (before=null, after=tick) and
    // deletion (before=tick, after=null). Pure updates (both non-null)
    // don't change the bitmap.
    bool const beforeIsTick = before && before->getType() == ltAMM_TICK;
    bool const afterIsTick = after && after->getType() == ltAMM_TICK;
    if (beforeIsTick != afterIsTick)
    {
        auto const& tickSle = afterIsTick ? after : before;
        if (tickSle->isFieldPresent(sfAMMID) &&
            tickSle->isFieldPresent(sfTickIndex))
        {
            tickLifecycles_.push_back(TickLifecycle{
                tickSle->getFieldH256(sfAMMID),
                tickSle->getFieldI32(sfTickIndex),
                afterIsTick});  // created => bit should be set
        }
    }
}

static bool
validBalances(
    STAmount const& amount,
    STAmount const& amount2,
    STAmount const& lptAMMBalance,
    ValidAMM::ZeroAllowed zeroAllowed)
{
    bool const positive =
        amount > beast::kZero && amount2 > beast::kZero && lptAMMBalance > beast::kZero;
    if (zeroAllowed == ValidAMM::ZeroAllowed::Yes)
    {
        return positive ||
            (amount == beast::kZero && amount2 == beast::kZero && lptAMMBalance == beast::kZero);
    }
    return positive;
}

bool
ValidAMM::finalizeVote(bool enforce, beast::Journal const& j) const
{
    if (lptAMMBalanceAfter_ != lptAMMBalanceBefore_ || ammPoolChanged_)
    {
        // LPTokens and the pool can not change on vote
        // LCOV_EXCL_START
        JLOG(j.error()) << "Invariant failed: AMMVote failed, "
                        << lptAMMBalanceBefore_.value_or(STAmount{}) << " "
                        << lptAMMBalanceAfter_.value_or(STAmount{}) << " " << ammPoolChanged_;
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::finalizeBid(bool enforce, beast::Journal const& j) const
{
    if (ammPoolChanged_)
    {
        // The pool can not change on bid
        // LCOV_EXCL_START
        JLOG(j.error()) << "Invariant failed: AMMBid failed, pool changed";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    // LPTokens are burnt, therefore there should be fewer LPTokens
    else if (
        lptAMMBalanceBefore_ && lptAMMBalanceAfter_ &&
        (*lptAMMBalanceAfter_ > *lptAMMBalanceBefore_ || *lptAMMBalanceAfter_ <= beast::kZero))
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "Invariant failed: AMMBid failed, " << *lptAMMBalanceBefore_ << " "
                        << *lptAMMBalanceAfter_;
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }

    return true;
}

bool
ValidAMM::finalizeCreate(
    STTx const& tx,
    ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (!ammAccount_)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "Invariant failed: AMMCreate failed, AMM object is not created";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    else
    {
        auto const [amount, amount2] = ammPoolHolds(
            view,
            *ammAccount_,
            tx[sfAmount].asset(),
            tx[sfAmount2].asset(),
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            j);
        // Create invariant:
        // sqrt(amount * amount2) == LPTokens (constant product)
        // For other curves, expected LP supply is curve-defined
        // all balances are greater than zero
        // NOLINTBEGIN(bugprone-unchecked-optional-access) lptAMMBalanceAfter_ set with ammAccount_
        // in visitEntry
        auto const lptIssue = lptAMMBalanceAfter_->get<Issue>();
        auto const curveType = getCurveType(*ammSle_);

        STAmount expectedLPT;
        if (curveType == CtConcentratedLiquidity || curveType == CtBinned)
        {
            // CL and Binned pools start empty — no asset transfer, no LP
            // token mint. Amount / Amount2 in the create tx define the
            // initial price ratio only. Liquidity arrives via AMMDeposit
            // (CL: position mints; Binned: per-bin MPT shares).
            expectedLPT = STAmount{lptIssue, 0};
        }
        else if (curveType == CtConstantProduct)
        {
            expectedLPT = ammLPTokens(amount, amount2, lptIssue);
        }
        else if (auto const* curve = getCurve(curveType, view.rules()))
        {
            auto const result = curve->initialLPTokens(amount, amount2, lptIssue, ammSle_.get());
            if (!result)
            {
                JLOG(j.error()) << "AMMCreate invariant failed: initialLPTokens error";
                if (enforce)
                    return false;
                return true;
            }
            expectedLPT = *result;
        }

        // CL and Binned pools may legitimately start with zero balances
        // — they don't take initial liquidity at create time. Other
        // curves must hold the just-deposited assets.
        auto const zeroAllowed =
            (curveType == CtConcentratedLiquidity || curveType == CtBinned)
            ? ZeroAllowed::Yes
            : ZeroAllowed::No;
        if (!validBalances(amount, amount2, *lptAMMBalanceAfter_, zeroAllowed) ||
            expectedLPT != *lptAMMBalanceAfter_)
        {
            JLOG(j.error()) << "Invariant failed: AMMCreate failed, " << amount << " " << amount2
                            << " " << *lptAMMBalanceAfter_;
            if (enforce)
                return false;
        }
        // NOLINTEND(bugprone-unchecked-optional-access)
    }

    return true;
}

bool
ValidAMM::finalizeDelete(bool enforce, bool enforceAMMDelete, TER res, beast::Journal const& j)
    const
{
    if (ammAccount_)
    {
        // LCOV_EXCL_START
        std::string const msg = (isTesSuccess(res)) ? "AMM object remained on tesSUCCESS"
                                                    : "AMM object changed on tecINCOMPLETE";
        JLOG(j.error()) << "Invariant failed: AMMDelete failed, " << msg;
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    if (enforceAMMDelete)
    {
        if (isTesSuccess(res))
        {
            if (!ammDeleted_)
            {
                // LCOV_EXCL_START
                JLOG(j.error())
                    << "Invariant failed: AMMDelete failed, AMM object remained on tesSUCCESS";
                return false;
                // LCOV_EXCL_STOP
            }
            if (!lptAMMBalanceBeforeDeletion_)
            {
                // LCOV_EXCL_START
                JLOG(j.error())
                    << "Invariant failed: AMMDelete failed, AMM object deleted without LP balance";
                return false;
                // LCOV_EXCL_STOP
            }
            if (*lptAMMBalanceBeforeDeletion_ != beast::kZero)
            {
                // LCOV_EXCL_START
                JLOG(j.error())
                    << "Invariant failed: AMMDelete failed, AMM object deleted with non-zero LP "
                       "balance: "
                    << *lptAMMBalanceBeforeDeletion_;
                return false;
                // LCOV_EXCL_STOP
            }
        }
        else if (ammDeleted_)
        {
            // AMM should only be fully deleted when AMMDelete returns tesSUCCESS.
            // LCOV_EXCL_START
            JLOG(j.error()) << "Invariant failed: AMMDelete failed, AMM object deleted when result "
                               "is not tesSUCCESS";
            return false;
            // LCOV_EXCL_STOP
        }
    }

    return true;
}

bool
ValidAMM::finalizeDEX(bool enforce, beast::Journal const& j) const
{
    if (!ammAccount_)
        return true;

    // CP and StableSwap derive their pool state from trustline balances —
    // a swap on those curves must not touch the AMM SLE. CL stores
    // currentTick, activeLiquidity, and feeGrowthGlobal0/1 on the AMM
    // SLE and they advance on every swap; only sfLPTokenBalance is
    // guaranteed invariant (CL doesn't mint LP tokens; it's always 0).
    // Binned advances sfActiveBinID and mutates per-bin SLE reserves;
    // sfLPTokenBalance is always 0 (no aggregate LP tokens).
    auto const curveType = ammSle_ ? getCurveType(*ammSle_) : CtConstantProduct;
    if (curveType == CtConcentratedLiquidity || curveType == CtBinned)
    {
        if (lptAMMBalanceAfter_ && *lptAMMBalanceAfter_ != beast::kZero)
        {
            JLOG(j.error())
                << "Invariant failed: AMM swap minted LP tokens, " << *lptAMMBalanceAfter_;
            if (enforce)
                return false;
        }
        return true;
    }

    // LCOV_EXCL_START
    JLOG(j.error()) << "Invariant failed: AMM swap failed, AMM object changed";
    if (enforce)
        return false;
    // LCOV_EXCL_STOP
    return true;
}

bool
ValidAMM::generalInvariant(
    xrpl::STTx const& tx,
    xrpl::ReadView const& view,
    ZeroAllowed zeroAllowed,
    beast::Journal const& j) const
{
    // NOLINTBEGIN(bugprone-unchecked-optional-access) ammAccount_ and lptAMMBalanceAfter_ set
    // together in visitEntry; callers only invoke this inside else-of-if(!ammAccount_)
    auto const [amount, amount2] = ammPoolHolds(
        view,
        *ammAccount_,
        tx[sfAsset],
        tx[sfAsset2],
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        j);
    // Deposit and Withdrawal invariant:
    // sqrt(amount * amount2) >= LPTokens (constant product)
    // For other curves, poolProductMean is curve-defined
    // all balances are greater than zero unless on last withdrawal
    auto const curveType = getCurveType(*ammSle_);

    // ConcentratedLiquidity has no fungible LP token supply. Ownership
    // is per-position; lptAMMBalance is always zero and the product /
    // LP-supply correspondence used by CP and StableSwap does not
    // apply. Validate the pool-side asset balances plus the structural
    // invariants that don't require iterating positions (audit #17, #23
    // partial). The full "sum(in-range position liquidity) ==
    // sfActiveLiquidity" invariant requires either iterating every
    // position SLE for the pool (no per-AMM positions index exists
    // today) or maintaining a parallel counter — both are out of scope
    // for this work block. The checks here catch the structural
    // desync cases that are observable from the AMM SLE alone.
    if (curveType == CtConcentratedLiquidity)
    {
        bool const balancesOk = (zeroAllowed == ZeroAllowed::Yes)
            ? (amount >= beast::kZero && amount2 >= beast::kZero)
            : (amount > beast::kZero && amount2 > beast::kZero);
        bool ok = balancesOk && *lptAMMBalanceAfter_ == beast::kZero;

        // sfPositionCount == 0 ⟹ sfActiveLiquidity == 0; conversely if
        // there is active liquidity there must be at least one position.
        auto const posCount = ammSle_->isFieldPresent(sfPositionCount)
            ? ammSle_->getFieldU32(sfPositionCount)
            : 0u;
        auto const activeLiq = ammSle_->isFieldPresent(sfActiveLiquidity)
            ? ammSle_->getFieldU64(sfActiveLiquidity)
            : 0u;
        if ((posCount == 0 && activeLiq != 0) || (activeLiq > 0 && posCount == 0))
            ok = false;

        // sfCurrentTick within global bounds.
        if (ammSle_->isFieldPresent(sfCurrentTick))
        {
            auto const ct = ammSle_->getFieldI32(sfCurrentTick);
            if (ct < minTick || ct > maxTick)
                ok = false;
        }

        if (!ok)
        {
            JLOG(j.error())
                << "Invariant failed: AMM CL " << tx.getTxnType() << " "
                << tx.getHash(HashPrefix::TransactionId) << " amounts " << amount
                << " " << amount2 << " lpt " << lptAMMBalanceAfter_->getText()
                << " posCount " << posCount << " activeLiq " << activeLiq;
            return false;
        }
        return true;
    }

    // Binned: no aggregate LP token supply; reserves track per-bin
    // contribution. Validate balances + that lptAMMBalance stays zero.
    // Full per-bin invariant ("sum of bin reserves == AMM holds") is
    // out of scope for sandbox (would require enumerating all bin SLEs).
    if (curveType == CtBinned)
    {
        bool const balancesOk = (zeroAllowed == ZeroAllowed::Yes)
            ? (amount >= beast::kZero && amount2 >= beast::kZero)
            : (amount >= beast::kZero && amount2 >= beast::kZero);
        bool const ok = balancesOk && *lptAMMBalanceAfter_ == beast::kZero;
        if (!ok)
        {
            JLOG(j.error())
                << "Invariant failed: AMM Binned " << tx.getTxnType() << " "
                << tx.getHash(HashPrefix::TransactionId) << " amounts " << amount
                << " " << amount2 << " lpt " << lptAMMBalanceAfter_->getText();
            return false;
        }
        return true;
    }

    Number poolProductMean;
    if (curveType == CtConstantProduct)
    {
        poolProductMean = root2(amount * amount2);
    }
    else if (auto const* curve = getCurve(curveType, view.rules()))
    {
        auto const result = curve->initialLPTokens(
            amount, amount2, lptAMMBalanceAfter_->get<Issue>(), ammSle_.get());
        if (result)
        {
            poolProductMean = Number{*result};
        }
        else
        {
            return false;
        }
    }
    bool const nonNegativeBalances =
        validBalances(amount, amount2, *lptAMMBalanceAfter_, zeroAllowed);
    auto const precisionLoss = checkAMMPrecisionLoss(poolProductMean, *lptAMMBalanceAfter_);
    if (!nonNegativeBalances || !isTesSuccess(precisionLoss))
    {
        JLOG(j.error()) << "Invariant failed: AMM " << tx.getTxnType() << " "
                        << tx.getHash(HashPrefix::TransactionId) << " " << ammPoolChanged_ << " "
                        << amount << " " << amount2 << " " << poolProductMean << " "
                        << lptAMMBalanceAfter_->getText() << " "
                        << ((*lptAMMBalanceAfter_ == beast::kZero)
                                ? Number{1}
                                : ((*lptAMMBalanceAfter_ - poolProductMean) / poolProductMean));
        return false;
    }
    // NOLINTEND(bugprone-unchecked-optional-access)

    return true;
}

bool
ValidAMM::finalizeDeposit(
    xrpl::STTx const& tx,
    xrpl::ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (!ammAccount_)
    {
        // LCOV_EXCL_START
        JLOG(j.error()) << "Invariant failed: AMMDeposit failed, AMM object is deleted";
        if (enforce)
            return false;
        // LCOV_EXCL_STOP
    }
    else if (!generalInvariant(tx, view, ZeroAllowed::No, j) && enforce)
    {
        return false;
    }

    return true;
}

bool
ValidAMM::finalizeWithdraw(
    xrpl::STTx const& tx,
    xrpl::ReadView const& view,
    bool enforce,
    bool enforceAMMDelete,
    beast::Journal const& j) const
{
    if (enforceAMMDelete && ammDeleted_)
    {
        // Last Withdraw or Clawback can delete the AMM. We don't have to check
        // the LPToken balance because a final AMMWithdraw or AMMClawback can
        // redeem the remaining LP tokens and delete the AMM entry in the same
        // transaction.
        return true;
    }
    if (ammAccount_ && !generalInvariant(tx, view, ZeroAllowed::Yes, j) && enforce)
    {
        return false;
    }

    return true;
}

bool
ValidAMM::finalizeFeeGrowthMonotonic(bool enforce, beast::Journal const& j) const
{
    // feeGrowthGlobal is "fees accumulated per unit of liquidity, ever".
    // Any transaction that decreases it is stealing from LPs. Only AMM SLEs
    // that have been touched in this tx carry the After value; if no Before
    // (the AMM didn't exist beforehand — e.g. AMMCreate) there's nothing to
    // compare against. CL is the only curve that writes these fields today;
    // CP/SS pools don't have them set, so the optionals stay empty and we
    // pass through.
    auto checkSide = [&](char const* side,
                         std::optional<Number> const& before,
                         std::optional<Number> const& after) {
        if (!before || !after)
            return true;
        if (*after >= *before)
            return true;
        JLOG(j.error()) << "Invariant failed: AMM feeGrowthGlobal" << side
                        << " decreased: " << *before << " -> " << *after;
        return false;
    };
    bool const ok0 =
        checkSide("0", feeGrowthGlobal0Before_, feeGrowthGlobal0After_);
    bool const ok1 =
        checkSide("1", feeGrowthGlobal1Before_, feeGrowthGlobal1After_);
    if (!ok0 || !ok1)
    {
        if (enforce)
            return false;
    }
    return true;
}

bool
ValidAMM::finalizeTickBitmapConsistency(
    ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (tickLifecycles_.empty())
        return true;
    bool ok = true;
    for (auto const& life : tickLifecycles_)
    {
        auto const [wordIdx, bitInWord] = tickToBitmapPos(life.tickIndex);
        auto const wordSle =
            view.read(keylet::ammTickBitmapWord(life.ammID, wordIdx));
        bool const actualBit = wordSle &&
            bitmapBitIsSet(wordSle->getFieldH256(sfBitmapBits), bitInWord);
        if (actualBit != life.expectedBitSet)
        {
            JLOG(j.error())
                << "Invariant failed: tick-bitmap mismatch for tick "
                << life.tickIndex << " in ammID " << life.ammID
                << " (expected bit " << (life.expectedBitSet ? "set" : "clear")
                << ", got " << (actualBit ? "set" : "clear") << ")";
            ok = false;
        }
    }
    if (!ok && enforce)
        return false;
    return true;
}

bool
ValidAMM::finalize(
    STTx const& tx,
    TER const result,
    XRPAmount const,
    ReadView const& view,
    beast::Journal const& j)
{
    // Delete may return tecINCOMPLETE if there are too many
    // trustlines to delete.
    if (!isTesSuccess(result) && result != tecINCOMPLETE)
        return true;

    bool const enforce = view.rules().enabled(fixAMMv1_3);
    bool const enforceAMMDelete = view.rules().enabled(fixCleanup3_3_0);

    // AMM can only be deleted by AMMWithdraw, AMMClawback, and AMMDelete
    if (enforceAMMDelete && ammDeleted_)
    {
        switch (tx.getTxnType())
        {
            case ttAMM_WITHDRAW:
            case ttAMM_CLAWBACK:
            case ttAMM_DELETE:
                break;
            default:
                // LCOV_EXCL_START
                JLOG(j.error()) << "Invariant failed: AMM failed, unexpected AMM deletion by "
                                << tx.getTxnType();
                return false;
                // LCOV_EXCL_STOP
        }
    }

    bool ok = true;
    switch (tx.getTxnType())
    {
        case ttAMM_CREATE:
            ok = finalizeCreate(tx, view, enforce, j);
            break;
        case ttAMM_DEPOSIT:
            ok = finalizeDeposit(tx, view, enforce, j);
            break;
        case ttAMM_CLAWBACK:
        case ttAMM_WITHDRAW:
            ok = finalizeWithdraw(tx, view, enforce, enforceAMMDelete, j);
            break;
        case ttAMM_BID:
            ok = finalizeBid(enforce, j);
            break;
        case ttAMM_VOTE:
            ok = finalizeVote(enforce, j);
            break;
        case ttAMM_DELETE:
            ok = finalizeDelete(enforce, enforceAMMDelete, result, j);
            break;
        case ttCHECK_CASH:
        case ttOFFER_CREATE:
        case ttPAYMENT:
            ok = finalizeDEX(enforce, j);
            break;
        default:
            break;
    }

    // feeGrowthGlobal monotonicity (audit #21) is universal — every tx
    // type that touched an AMM SLE must respect it.
    if (!finalizeFeeGrowthMonotonic(enforce, j))
        ok = false;

    // CL tick bitmap consistency — every tick SLE lifecycle change must
    // be mirrored in the bitmap. Universal: applies to AMMDeposit and
    // AMMWithdraw (the writers), plus catches any future code path that
    // mutates tick SLEs without maintaining the bitmap.
    if (!finalizeTickBitmapConsistency(view, enforce, j))
        ok = false;

    // Binned-pool consistency: bin reserves must sum to AMM trustline
    // balances, and sfActiveBinID must reference a valid bin (or the
    // pool must be empty). Cheap check — only fires when an AMM SLE was
    // touched and its curveType is CtBinned.
    if (!finalizeBinnedConsistency(view, enforce, j))
        ok = false;

    return ok;
}

bool
ValidAMM::finalizeBinnedConsistency(
    ReadView const& view,
    bool enforce,
    beast::Journal const& j) const
{
    if (!ammAccount_ || !ammSle_)
        return true;
    auto const curveType = getCurveType(*ammSle_);
    if (curveType != CtBinned)
        return true;

    // Sum per-bin reserves by walking the AMM pseudo-account's owner
    // directory looking for ltAMM_BIN entries.
    Number sumR0{0};
    Number sumR1{0};
    bool sawActiveBin = false;
    std::int32_t const activeBinID = ammSle_->isFieldPresent(sfActiveBinID)
        ? ammSle_->getFieldI32(sfActiveBinID)
        : 0;

    auto const ammID = ammSle_->key();
    bool outstandingDrift = false;
    bool activeBinIsEmpty = false;
    forEachItem(view, *ammAccount_, [&](std::shared_ptr<SLE const> const& sle) {
        if (!sle || sle->getType() != ltAMM_BIN)
            return;
        if (!sle->isFieldPresent(sfAMMID) ||
            sle->getFieldH256(sfAMMID) != ammID)
            return;
        sumR0 += Number{sle->getFieldAmount(sfReserve0)};
        sumR1 += Number{sle->getFieldAmount(sfReserve1)};
        if (sle->getFieldI32(sfBinID) == activeBinID)
        {
            sawActiveBin = true;
            // P0-7: if any bin has shares, the active bin must too.
            if (sle->getFieldU64(sfOutstandingAmount) == 0)
                activeBinIsEmpty = true;
        }
        // P0-3: bin's sfOutstandingAmount must equal its MPT issuance's
        // sfOutstandingAmount — the two are independently mutated and
        // any drift means a state-corruption bug somewhere.
        if (sle->isFieldPresent(sfMPTokenIssuanceID))
        {
            auto const mptId = sle->getFieldH192(sfMPTokenIssuanceID);
            auto const iss = view.read(keylet::mptokenIssuance(mptId));
            if (iss)
            {
                auto const binOut = sle->getFieldU64(sfOutstandingAmount);
                auto const issOut = iss->getFieldU64(sfOutstandingAmount);
                if (binOut != issOut)
                    outstandingDrift = true;
            }
        }
    });

    auto const asset0 = (*ammSle_)[sfAsset];
    auto const asset1 = (*ammSle_)[sfAsset2];
    auto const trust0 = ammAccountHolds(view, *ammAccount_, asset0);
    auto const trust1 = ammAccountHolds(view, *ammAccount_, asset1);

    // Tolerance: rounding errors accumulate per swap step.
    auto const close = [](Number const& a, Number const& b) {
        if (a == b)
            return true;
        Number const diff = a > b ? a - b : b - a;
        Number const ref = a > b ? a : b;
        if (ref == Number{0})
            return diff <= Number{1, -9};
        return (diff / ref) < Number{1, -6};
    };

    if (!close(sumR0, Number{trust0}) || !close(sumR1, Number{trust1}))
    {
        JLOG(j.error())
            << "Invariant failed: Binned AMM reserves don't sum to trustlines. "
            << "sum(R0)=" << sumR0 << " trust0=" << trust0
            << " sum(R1)=" << sumR1 << " trust1=" << trust1;
        if (enforce)
            return false;
    }

    // Active-bin sanity: if there are bins, the active bin must be one
    // of them. If no bins exist, activeBinID is moot — it stays at its
    // last value (or 0 if never deposited).
    bool const hasBins = sumR0 > Number{0} || sumR1 > Number{0};
    if (hasBins && !sawActiveBin)
    {
        JLOG(j.error())
            << "Invariant failed: Binned AMM has reserves but activeBinID "
            << activeBinID << " does not reference an existing bin";
        if (enforce)
            return false;
    }

    // P0-7: active bin must hold shares whenever any bin in the pool
    // does — this is the strict form of "activeBinID tracks the
    // current price." Caught by the nearest-bin advance logic in
    // AMMWithdraw; this invariant pins it.
    if (hasBins && sawActiveBin && activeBinIsEmpty)
    {
        JLOG(j.error())
            << "Invariant failed: Binned AMM activeBinID " << activeBinID
            << " references an empty bin while other bins hold shares";
        if (enforce)
            return false;
    }

    // P0-3: bin sfOutstandingAmount must equal issuance sfOutstandingAmount.
    if (outstandingDrift)
    {
        JLOG(j.error())
            << "Invariant failed: Binned AMM bin sfOutstandingAmount drifted "
               "from MPT issuance sfOutstandingAmount";
        if (enforce)
            return false;
    }

    return true;
}

}  // namespace xrpl
