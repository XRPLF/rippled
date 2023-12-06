//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/Log.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/conditions/Condition.h>
#include <ripple/conditions/Fulfillment.h>
#include <ripple/ledger/View.h>
#include <ripple/plugin/exports.h>
#include <ripple/plugin/reset.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/st.h>
#include <map>

using namespace ripple;

static const std::uint16_t ltNEW_ESCROW = 0x0001;
static const std::uint16_t NEW_ESCROW_NAMESPACE = 't';

template <class... Args>
static uint256
indexHash(std::uint16_t space, Args const&... args)
{
    return sha512Half(space, args...);
}

Keylet
new_escrow(AccountID const& src, std::uint32_t seq) noexcept
{
    return {ltNEW_ESCROW, indexHash(NEW_ESCROW_NAMESPACE, src, seq)};
}

static uint256 newEscrowCreateAmendment;

/** Has the specified time passed?

    @param now  the current time
    @param mark the cutoff point
    @return true if \a now refers to a time strictly after \a mark, else false.
*/
static inline bool
after(NetClock::time_point now, std::uint32_t mark)
{
    return now.time_since_epoch().count() > mark;
}

XRPAmount
calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // Returns the fee in fee units.

    // The computation has two parts:
    //  * The base fee, which is the same for most transactions.
    //  * The additional cost of each multisignature on the transaction.
    XRPAmount const baseFee = view.fees().base;

    // Each signer adds one more baseFee to the minimum required fee
    // for the transaction.
    std::size_t const signerCount =
        tx.isFieldPresent(sfSigners) ? tx.getFieldArray(sfSigners).size() : 0;

    return baseFee + (signerCount * baseFee);
}

NotTEC
preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(newEscrowCreateAmendment))
        return temDISABLED;

    if (ctx.rules.enabled(fix1543) && ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (!isXRP(ctx.tx[sfAmount]))
        return temBAD_AMOUNT;

    // This uncommented allows us to trigger the invariant check
    // if (ctx.tx[sfAmount] < beast::zero)
    //     return temBAD_AMOUNT;

    // We must specify at least one timeout value
    if (!ctx.tx[~sfCancelAfter] && !ctx.tx[~sfFinishAfter])
        return temBAD_EXPIRATION;

    // If both finish and cancel times are specified then the cancel time must
    // be strictly after the finish time.
    if (ctx.tx[~sfCancelAfter] && ctx.tx[~sfFinishAfter] &&
        ctx.tx[sfCancelAfter] <= ctx.tx[sfFinishAfter])
        return temBAD_EXPIRATION;

    if (ctx.rules.enabled(fix1571))
    {
        // In the absence of a FinishAfter, the escrow can be finished
        // immediately, which can be confusing. When creating an escrow,
        // we want to ensure that either a FinishAfter time is explicitly
        // specified or a completion condition is attached.
        if (!ctx.tx[~sfFinishAfter] && !ctx.tx[~sfCondition])
            return temMALFORMED;
    }

    // TODO: get conditions working
    // if (auto const cb = ctx.tx[~sfCondition])
    // {
    //     using namespace cryptoconditions;

    //     std::error_code ec;

    //     auto condition = Condition::deserialize(*cb, ec);
    //     if (!condition)
    //     {
    //         JLOG(ctx.j.debug())
    //             << "Malformed condition during escrow creation: "
    //             << ec.message();
    //         return temMALFORMED;
    //     }

    //     // Conditions other than PrefixSha256 require the
    //     // "CryptoConditionsSuite" amendment:
    //     if (condition->type != Type::preimageSha256 &&
    //         !ctx.rules.enabled(featureCryptoConditionsSuite))
    //         return temDISABLED;
    // }

    return preflight2(ctx);
}

TER
preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
}

TER
doApply(ApplyContext& ctx, XRPAmount mPriorBalance, XRPAmount mSourceBalance)
{
    auto const closeTime = ctx.view().info().parentCloseTime;

    // Prior to fix1571, the cancel and finish times could be greater
    // than or equal to the parent ledgers' close time.
    //
    // With fix1571, we require that they both be strictly greater
    // than the parent ledgers' close time.
    if (ctx.view().rules().enabled(fix1571))
    {
        if (ctx.tx[~sfCancelAfter] && after(closeTime, ctx.tx[sfCancelAfter]))
            return tecNO_PERMISSION;

        if (ctx.tx[~sfFinishAfter] && after(closeTime, ctx.tx[sfFinishAfter]))
            return tecNO_PERMISSION;
    }
    else
    {
        if (ctx.tx[~sfCancelAfter])
        {
            auto const cancelAfter = ctx.tx[sfCancelAfter];

            if (closeTime.time_since_epoch().count() >= cancelAfter)
                return tecNO_PERMISSION;
        }

        if (ctx.tx[~sfFinishAfter])
        {
            auto const finishAfter = ctx.tx[sfFinishAfter];

            if (closeTime.time_since_epoch().count() >= finishAfter)
                return tecNO_PERMISSION;
        }
    }

    auto const account = ctx.tx[sfAccount];
    auto const sle = ctx.view().peek(keylet::account(account));
    if (!sle)
        return tefINTERNAL;

    // Check reserve and funds availability
    {
        auto const balance = STAmount((*sle)[sfBalance]).xrp();
        auto const reserve =
            ctx.view().fees().accountReserve((*sle)[sfOwnerCount] + 1);

        if (balance < reserve)
            return tecINSUFFICIENT_RESERVE;

        if (balance < reserve + STAmount(ctx.tx[sfAmount]).xrp())
            return tecUNFUNDED;
    }

    // Check destination account
    {
        auto const sled =
            ctx.view().read(keylet::account(ctx.tx[sfDestination]));
        if (!sled)
            return tecNO_DST;
        if (((*sled)[sfFlags] & lsfRequireDestTag) &&
            !ctx.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;

        // Obeying the lsfDisallowXRP flag was a bug.  Piggyback on
        // featureDepositAuth to remove the bug.
        if (!ctx.view().rules().enabled(featureDepositAuth) &&
            ((*sled)[sfFlags] & lsfDisallowXRP))
            return tecNO_TARGET;
    }

    // Create escrow in ledger.  Note that we we use the value from the
    // sequence or ticket.  For more explanation see comments in SeqProxy.h.
    Keylet const escrowKeylet =
        new_escrow(account, ctx.tx.getSeqProxy().value());
    auto const slep = std::make_shared<SLE>(escrowKeylet);
    (*slep)[sfAmount] = ctx.tx[sfAmount];
    (*slep)[sfAccount] = account;
    (*slep)[~sfCondition] = ctx.tx[~sfCondition];
    (*slep)[~sfSourceTag] = ctx.tx[~sfSourceTag];
    (*slep)[sfDestination] = ctx.tx[sfDestination];
    (*slep)[~sfCancelAfter] = ctx.tx[~sfCancelAfter];
    (*slep)[~sfFinishAfter] = ctx.tx[~sfFinishAfter];
    (*slep)[~sfDestinationTag] = ctx.tx[~sfDestinationTag];

    ctx.view().insert(slep);

    // Add escrow to sender's owner directory
    {
        auto page = ctx.view().dirInsert(
            keylet::ownerDir(account), escrowKeylet, describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;
        (*slep)[sfOwnerNode] = *page;
    }

    // If it's not a self-send, add escrow to recipient's owner directory.
    if (auto const dest = ctx.tx[sfDestination]; dest != ctx.tx[sfAccount])
    {
        auto page = ctx.view().dirInsert(
            keylet::ownerDir(dest), escrowKeylet, describeOwnerDir(dest));
        if (!page)
            return tecDIR_FULL;
        (*slep)[sfDestinationNode] = *page;
    }

    // Deduct owner's balance, increment owner count
    (*sle)[sfBalance] = (*sle)[sfBalance] - ctx.tx[sfAmount];
    adjustOwnerCount(ctx.view(), sle, 1, ctx.journal);
    ctx.view().update(sle);

    return tesSUCCESS;
}

extern "C" Container<TransactorExport>
getTransactors()
{
    static SOElementExport format[] = {
        {sfDestination.getCode(), soeREQUIRED},
        {sfAmount.getCode(), soeREQUIRED},
        {sfCondition.getCode(), soeOPTIONAL},
        {sfCancelAfter.getCode(), soeOPTIONAL},
        {sfFinishAfter.getCode(), soeOPTIONAL},
        {sfDestinationTag.getCode(), soeOPTIONAL}};
    SOElementExport* formatPtr = format;
    static TransactorExport list[] = {
        {"NewEscrowCreate",
         61,
         {formatPtr, 6},
         ConsequencesFactoryType::Normal,
         NULL,
         calculateBaseFee,
         preflight,
         preclaim,
         doApply,
         NULL,
         NULL,
         NULL,
         NULL}};
    TransactorExport* ptr = list;
    return {ptr, 1};
}

std::int64_t
visitEntryXRPChange(
    bool isDelete,
    std::shared_ptr<SLE const> const& entry,
    bool isBefore)
{
    if (isBefore)
    {
        return -1 * (*entry)[sfAmount].xrp().drops();
    }
    if (isDelete)
        return 0;
    return (*entry)[sfAmount].xrp().drops();
}

class NoZeroNewEscrow
{
public:
    static std::map<void*, NoZeroNewEscrow&> checks;
    static void
    visitEntryExport(
        void* id,
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after)
    {
        if (auto it = checks.find(id); it != checks.end())
        {
            return it->second.visitEntry(isDelete, before, after);
        }
        NoZeroNewEscrow* check = new NoZeroNewEscrow();
        check->visitEntry(isDelete, before, after);
        checks.insert({id, *check});
    }

    static bool
    finalizeExport(
        void* id,
        STTx const& tx,
        TER const result,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j)
    {
        if (auto it = checks.find(id); it != checks.end())
        {
            bool const finalizeResult =
                it->second.finalize(tx, result, fee, view, j);
            checks.erase(id);
            return finalizeResult;
        }
        JLOG(j.fatal()) << "Invariant failed: could not find matching ID";
        return false;
    }

private:
    bool bad_ = false;

    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after)
    {
        auto isBad = [](STAmount const& amount) {
            if (!amount.native())
                return true;

            if (amount.xrp() <= XRPAmount{0})
                return true;

            if (amount.xrp() >= INITIAL_XRP)
                return true;

            return false;
        };

        if (before && before->getType() == ltNEW_ESCROW)
            bad_ |= isBad((*before)[sfAmount]);

        if (after && after->getType() == ltNEW_ESCROW)
            bad_ |= isBad((*after)[sfAmount]);
    }

    bool
    finalize(
        STTx const&,
        TER const,
        XRPAmount const,
        ReadView const&,
        beast::Journal const& j)
    {
        if (bad_)
        {
            JLOG(j.fatal())
                << "Invariant failed: new escrow specifies invalid amount";
            return false;
        }

        return true;
    }
};
std::map<void*, NoZeroNewEscrow&> NoZeroNewEscrow::checks{};

extern "C" Container<LedgerObjectExport>
getLedgerObjects()
{
    static SOElementExport format[] = {
        {sfAccount.getCode(), soeREQUIRED},
        {sfDestination.getCode(), soeREQUIRED},
        {sfAmount.getCode(), soeREQUIRED},
        {sfCondition.getCode(), soeOPTIONAL},
        {sfCancelAfter.getCode(), soeOPTIONAL},
        {sfFinishAfter.getCode(), soeOPTIONAL},
        {sfSourceTag.getCode(), soeOPTIONAL},
        {sfDestinationTag.getCode(), soeOPTIONAL},
        {sfOwnerNode.getCode(), soeREQUIRED},
        {sfPreviousTxnID.getCode(), soeREQUIRED},
        {sfPreviousTxnLgrSeq.getCode(), soeREQUIRED},
        {sfDestinationNode.getCode(), soeOPTIONAL},
    };
    static LedgerObjectExport list[] = {
        {ltNEW_ESCROW,
         "NewEscrow",
         "new_escrow",
         {format, 12},
         true,
         NULL,
         visitEntryXRPChange}};
    LedgerObjectExport* ptr = list;
    return {ptr, 1};
}

extern "C" Container<InvariantCheckExport>
getInvariantChecks()
{
    static InvariantCheckExport list[] = {{
        NoZeroNewEscrow::visitEntryExport,
        NoZeroNewEscrow::finalizeExport,
    }};
    InvariantCheckExport* ptr = list;
    return {ptr, 1};
}

extern "C" Container<AmendmentExport>
getAmendments()
{
    reinitialize();
    resetPlugins();
    NoZeroNewEscrow::checks.clear();
    AmendmentExport const amendment = {
        "featurePluginTest2",
        true,
        VoteBehavior::DefaultNo,
    };
    newEscrowCreateAmendment = registerPluginAmendment(amendment);
    static AmendmentExport list[] = {amendment};
    AmendmentExport* ptr = list;
    return {ptr, 1};
}
