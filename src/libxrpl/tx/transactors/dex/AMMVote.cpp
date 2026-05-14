/** @file
 *  Implementation of the AMMVote transactor — on-chain fee governance for
 *  AMM liquidity pools.
 *
 *  The file-scope `applyVote` static function contains the full slot-management
 *  and weighted-average fee recalculation logic; the `AMMVote` class methods
 *  delegate to it after performing amendment gating, stateless preflight
 *  validation, and ledger-state preclaim checks.
 */
#include <xrpl/tx/transactors/dex/AMMVote.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl {

bool
AMMVote::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return false;

    return ctx.rules.enabled(featureMPTokensV2) ||
        (!ctx.tx[sfAsset].holds<MPTIssue>() && !ctx.tx[sfAsset2].holds<MPTIssue>());
}

NotTEC
AMMVote::preflight(PreflightContext const& ctx)
{
    if (auto const res = invalidAMMAssetPair(ctx.tx[sfAsset], ctx.tx[sfAsset2]))
    {
        JLOG(ctx.j.debug()) << "AMM Vote: invalid asset pair.";
        return res;
    }

    if (ctx.tx[sfTradingFee] > kTRADING_FEE_THRESHOLD)
    {
        JLOG(ctx.j.debug()) << "AMM Vote: invalid trading fee.";
        return temBAD_FEE;
    }

    return tesSUCCESS;
}

TER
AMMVote::preclaim(PreclaimContext const& ctx)
{
    auto const ammSle = ctx.view.read(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Vote: Invalid asset pair.";
        return terNO_AMM;
    }
    if (ammSle->getFieldAmount(sfLPTokenBalance) == beast::kZERO)
    {
        return tecAMM_EMPTY;
    }
    if (auto const lpTokensNew = ammLPHolds(ctx.view, *ammSle, ctx.tx[sfAccount], ctx.j);
        lpTokensNew == beast::kZERO)
    {
        JLOG(ctx.j.debug()) << "AMM Vote: account is not LP.";
        return tecAMM_INVALID_TOKENS;
    }

    return tesSUCCESS;
}

/** Execute the vote slot update and weighted-average fee recalculation.
 *
 *  Iterates the AMM's existing `sfVoteSlots` array, re-evaluating each
 *  voter's current LP token balance.  Entries where the balance has dropped
 *  to zero are silently evicted (passive pruning — no separate cleanup
 *  transaction is needed).  For the submitting `account`, the fee and token
 *  balance are updated in place when an existing entry is found.
 *
 *  When the submitting account is new and all `kVOTE_MAX_SLOTS` (8) slots
 *  are occupied, eviction of the weakest current voter is attempted.  The
 *  eviction criterion is: the newcomer must hold **more** LP tokens than the
 *  minimum-token holder, or equal tokens and a **higher** proposed fee.  Ties
 *  between existing voters are broken deterministically by (tokens, fee,
 *  accountID) so every validator reaches the same decision.  If the newcomer
 *  does not meet the threshold, no slot is written but the transaction still
 *  succeeds — the fee recalculation over refreshed balances is still applied.
 *
 *  After slot maintenance, the new effective trading fee is computed as
 *  `sum(fee_i * tokens_i) / sum(tokens_i)` using `Number` (arbitrary-precision
 *  rational arithmetic) and truncated to `std::int64_t`.  Both `sfTradingFee`
 *  and `sfDiscountedFee` inside `sfAuctionSlot` are updated; when either
 *  rounds to zero the field is explicitly removed via `makeFieldAbsent` rather
 *  than stored as zero, because absent and present-but-zero serialize
 *  differently in XRPL's canonical binary format.
 *
 *  @param ctx      Apply context providing the transaction, rules, and journal.
 *  @param sb       Sandbox view accumulating all ledger mutations; the caller
 *      commits it only on success.
 *  @param account  AccountID of the LP submitting the vote.
 *  @param j        Journal for diagnostic logging.
 *  @return A `(TER, bool)` pair where the second element is `true` iff the
 *      caller should flush `sb` to the real ledger view.  Returns
 *      `{tecINTERNAL, false}` if the AMM SLE cannot be peeked, which
 *      indicates ledger corruption and is unreachable under normal operation.
 *  @note The `XRPL_ASSERT` before the fee-write phase enforces that
 *      `sfAuctionSlot` is present whenever the `fixInnerObjTemplate`
 *      amendment is active; a missing slot at that point would indicate a
 *      malformed AMM SLE.
 */
static std::pair<TER, bool>
applyVote(ApplyContext& ctx, Sandbox& sb, AccountID const& account, beast::Journal j)
{
    auto const feeNew = ctx.tx[sfTradingFee];
    auto ammSle = sb.peek(keylet::amm(ctx.tx[sfAsset], ctx.tx[sfAsset2]));
    if (!ammSle)
        return {tecINTERNAL, false};
    STAmount const lptAMMBalance = (*ammSle)[sfLPTokenBalance];
    auto const lpTokensNew = ammLPHolds(sb, *ammSle, account, ctx.journal);
    std::optional<STAmount> minTokens;
    std::size_t minPos{0};
    AccountID minAccount{0};
    std::uint32_t minFee{0};
    STArray updatedVoteSlots;
    Number num{0};
    Number den{0};
    bool foundAccount = false;

    for (auto const& entry : ammSle->getFieldArray(sfVoteSlots))
    {
        auto const entryAccount = entry[sfAccount];
        auto lpTokens = ammLPHolds(sb, *ammSle, entryAccount, ctx.journal);
        if (lpTokens == beast::kZERO)
        {
            JLOG(j.debug()) << "AMMVote::applyVote, account " << entryAccount << " is not LP";
            continue;
        }
        auto feeVal = entry[sfTradingFee];
        STObject newEntry = STObject::makeInnerObject(sfVoteEntry);
        if (entryAccount == account)
        {
            lpTokens = lpTokensNew;
            feeVal = feeNew;
            foundAccount = true;
        }
        num += feeVal * lpTokens;
        den += lpTokens;
        newEntry.setAccountID(sfAccount, entryAccount);
        if (feeVal != 0)
            newEntry.setFieldU16(sfTradingFee, feeVal);
        newEntry.setFieldU32(
            sfVoteWeight,
            static_cast<std::int64_t>(
                Number(lpTokens) * kVOTE_WEIGHT_SCALE_FACTOR / lptAMMBalance));

        if (!minTokens ||
            (lpTokens < *minTokens ||
             (lpTokens == *minTokens &&
              (feeVal < minFee || (feeVal == minFee && entryAccount < minAccount)))))
        {
            minTokens = lpTokens;
            minPos = updatedVoteSlots.size();
            minAccount = entryAccount;
            minFee = feeVal;
        }
        updatedVoteSlots.pushBack(std::move(newEntry));
    }

    if (!foundAccount)
    {
        auto update = [&](std::optional<std::uint8_t> const& minPos = std::nullopt) {
            STObject newEntry = STObject::makeInnerObject(sfVoteEntry);
            if (feeNew != 0)
                newEntry.setFieldU16(sfTradingFee, feeNew);
            newEntry.setFieldU32(
                sfVoteWeight,
                static_cast<std::int64_t>(
                    Number(lpTokensNew) * kVOTE_WEIGHT_SCALE_FACTOR / lptAMMBalance));
            newEntry.setAccountID(sfAccount, account);
            num += feeNew * lpTokensNew;
            den += lpTokensNew;
            if (minPos)
            {
                *(updatedVoteSlots.begin() + *minPos) = std::move(newEntry);
            }
            else
            {
                updatedVoteSlots.pushBack(std::move(newEntry));
            }
        };
        if (updatedVoteSlots.size() < kVOTE_MAX_SLOTS)
        {
            update();
        }
        // NOLINTBEGIN(bugprone-unchecked-optional-access) slots full means loop ran, minTokens is
        // set
        else if (lpTokensNew > *minTokens || (lpTokensNew == *minTokens && feeNew > minFee))
        {
            auto const entry = updatedVoteSlots.begin() + minPos;
            num -= Number((*entry)[~sfTradingFee].valueOr(0)) * *minTokens;
            den -= *minTokens;
            update(minPos);
        }
        // NOLINTEND(bugprone-unchecked-optional-access)
        else
        {
            JLOG(j.debug()) << "AMMVote::applyVote, insufficient tokens to "
                               "override other votes";
        }
    }

    XRPL_ASSERT(
        !ctx.view().rules().enabled(fixInnerObjTemplate) || ammSle->isFieldPresent(sfAuctionSlot),
        "xrpl::applyVote : has auction slot");

    ammSle->setFieldArray(sfVoteSlots, updatedVoteSlots);
    if (auto const fee = static_cast<std::int64_t>(num / den))
    {
        ammSle->setFieldU16(sfTradingFee, fee);
        if (ammSle->isFieldPresent(sfAuctionSlot))
        {
            auto& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
            if (auto const discountedFee = fee / kAUCTION_SLOT_DISCOUNTED_FEE_FRACTION)
            {
                auctionSlot.setFieldU16(sfDiscountedFee, discountedFee);
            }
            else if (auctionSlot.isFieldPresent(sfDiscountedFee))
            {
                auctionSlot.makeFieldAbsent(sfDiscountedFee);
            }
        }
    }
    else
    {
        if (ammSle->isFieldPresent(sfTradingFee))
            ammSle->makeFieldAbsent(sfTradingFee);
        if (ammSle->isFieldPresent(sfAuctionSlot))
        {
            auto& auctionSlot = ammSle->peekFieldObject(sfAuctionSlot);
            if (auctionSlot.isFieldPresent(sfDiscountedFee))
                auctionSlot.makeFieldAbsent(sfDiscountedFee);
        }
    }
    sb.update(ammSle);

    return {tesSUCCESS, true};
}

TER
AMMVote::doApply()
{
    Sandbox sb(&ctx_.view());

    auto const result = applyVote(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

void
AMMVote::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
AMMVote::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
