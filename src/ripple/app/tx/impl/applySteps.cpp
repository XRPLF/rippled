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

#include <ripple/app/tx/applySteps.h>
#include <ripple/app/tx/impl/AMMBid.h>
#include <ripple/app/tx/impl/AMMCreate.h>
#include <ripple/app/tx/impl/AMMDelete.h>
#include <ripple/app/tx/impl/AMMDeposit.h>
#include <ripple/app/tx/impl/AMMVote.h>
#include <ripple/app/tx/impl/AMMWithdraw.h>
#include <ripple/app/tx/impl/ApplyContext.h>
#include <ripple/app/tx/impl/CancelCheck.h>
#include <ripple/app/tx/impl/CancelOffer.h>
#include <ripple/app/tx/impl/CashCheck.h>
#include <ripple/app/tx/impl/Change.h>
#include <ripple/app/tx/impl/Clawback.h>
#include <ripple/app/tx/impl/CreateCheck.h>
#include <ripple/app/tx/impl/CreateOffer.h>
#include <ripple/app/tx/impl/CreateTicket.h>
#include <ripple/app/tx/impl/DID.h>
#include <ripple/app/tx/impl/DeleteAccount.h>
#include <ripple/app/tx/impl/DeleteOracle.h>
#include <ripple/app/tx/impl/DepositPreauth.h>
#include <ripple/app/tx/impl/Escrow.h>
#include <ripple/app/tx/impl/NFTokenAcceptOffer.h>
#include <ripple/app/tx/impl/NFTokenBurn.h>
#include <ripple/app/tx/impl/NFTokenCancelOffer.h>
#include <ripple/app/tx/impl/NFTokenCreateOffer.h>
#include <ripple/app/tx/impl/NFTokenMint.h>
#include <ripple/app/tx/impl/PayChan.h>
#include <ripple/app/tx/impl/Payment.h>
#include <ripple/app/tx/impl/SetAccount.h>
#include <ripple/app/tx/impl/SetOracle.h>
#include <ripple/app/tx/impl/SetRegularKey.h>
#include <ripple/app/tx/impl/SetSignerList.h>
#include <ripple/app/tx/impl/SetTrust.h>
#include <ripple/app/tx/impl/XChainBridge.h>
#include <ripple/protocol/TxFormats.h>

#include <stdexcept>

namespace ripple {

namespace {

struct UnknownTxnType : std::exception
{
    TxType txnType;
    UnknownTxnType(TxType t) : txnType{t}
    {
    }
};

// Call a lambda with the concrete transaction type as a template parameter
// throw an "UnknownTxnType" exception on error
template <class F>
auto
with_txn_type(TxType txnType, F&& f)
{
    switch (txnType)
    {
        case ttACCOUNT_DELETE:
            return f.template operator()<DeleteAccount>();
        case ttACCOUNT_SET:
            return f.template operator()<SetAccount>();
        case ttCHECK_CANCEL:
            return f.template operator()<CancelCheck>();
        case ttCHECK_CASH:
            return f.template operator()<CashCheck>();
        case ttCHECK_CREATE:
            return f.template operator()<CreateCheck>();
        case ttDEPOSIT_PREAUTH:
            return f.template operator()<DepositPreauth>();
        case ttOFFER_CANCEL:
            return f.template operator()<CancelOffer>();
        case ttOFFER_CREATE:
            return f.template operator()<CreateOffer>();
        case ttESCROW_CREATE:
            return f.template operator()<EscrowCreate>();
        case ttESCROW_FINISH:
            return f.template operator()<EscrowFinish>();
        case ttESCROW_CANCEL:
            return f.template operator()<EscrowCancel>();
        case ttPAYCHAN_CLAIM:
            return f.template operator()<PayChanClaim>();
        case ttPAYCHAN_CREATE:
            return f.template operator()<PayChanCreate>();
        case ttPAYCHAN_FUND:
            return f.template operator()<PayChanFund>();
        case ttPAYMENT:
            return f.template operator()<Payment>();
        case ttREGULAR_KEY_SET:
            return f.template operator()<SetRegularKey>();
        case ttSIGNER_LIST_SET:
            return f.template operator()<SetSignerList>();
        case ttTICKET_CREATE:
            return f.template operator()<CreateTicket>();
        case ttTRUST_SET:
            return f.template operator()<SetTrust>();
        case ttAMENDMENT:
        case ttFEE:
        case ttUNL_MODIFY:
            return f.template operator()<Change>();
        case ttNFTOKEN_MINT:
            return f.template operator()<NFTokenMint>();
        case ttNFTOKEN_BURN:
            return f.template operator()<NFTokenBurn>();
        case ttNFTOKEN_CREATE_OFFER:
            return f.template operator()<NFTokenCreateOffer>();
        case ttNFTOKEN_CANCEL_OFFER:
            return f.template operator()<NFTokenCancelOffer>();
        case ttNFTOKEN_ACCEPT_OFFER:
            return f.template operator()<NFTokenAcceptOffer>();
        case ttCLAWBACK:
            return f.template operator()<Clawback>();
        case ttAMM_CREATE:
            return f.template operator()<AMMCreate>();
        case ttAMM_DEPOSIT:
            return f.template operator()<AMMDeposit>();
        case ttAMM_WITHDRAW:
            return f.template operator()<AMMWithdraw>();
        case ttAMM_VOTE:
            return f.template operator()<AMMVote>();
        case ttAMM_BID:
            return f.template operator()<AMMBid>();
        case ttAMM_DELETE:
            return f.template operator()<AMMDelete>();
        case ttXCHAIN_CREATE_BRIDGE:
            return f.template operator()<XChainCreateBridge>();
        case ttXCHAIN_MODIFY_BRIDGE:
            return f.template operator()<BridgeModify>();
        case ttXCHAIN_CREATE_CLAIM_ID:
            return f.template operator()<XChainCreateClaimID>();
        case ttXCHAIN_COMMIT:
            return f.template operator()<XChainCommit>();
        case ttXCHAIN_CLAIM:
            return f.template operator()<XChainClaim>();
        case ttXCHAIN_ADD_CLAIM_ATTESTATION:
            return f.template operator()<XChainAddClaimAttestation>();
        case ttXCHAIN_ADD_ACCOUNT_CREATE_ATTESTATION:
            return f.template operator()<XChainAddAccountCreateAttestation>();
        case ttXCHAIN_ACCOUNT_CREATE_COMMIT:
            return f.template operator()<XChainCreateAccountCommit>();
        case ttDID_SET:
            return f.template operator()<DIDSet>();
        case ttDID_DELETE:
            return f.template operator()<DIDDelete>();
        case ttORACLE_SET:
            return f.template operator()<SetOracle>();
        case ttORACLE_DELETE:
            return f.template operator()<DeleteOracle>();
        default:
            throw UnknownTxnType(txnType);
    }
}
}  // namespace

// Templates so preflight does the right thing with T::ConsequencesFactory.
//
// This could be done more easily using if constexpr, but Visual Studio
// 2017 doesn't handle if constexpr correctly.  So once we're no longer
// building with Visual Studio 2017 we can consider replacing the four
// templates with a single template function that uses if constexpr.
//
// For Transactor::Normal
//

// clang-format off
// Current formatter for rippled is based on clang-10, which does not handle `requires` clauses
template <class T>
requires(T::ConsequencesFactory == Transactor::Normal)
TxConsequences
    consequences_helper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx);
};

// For Transactor::Blocker
template <class T>
requires(T::ConsequencesFactory == Transactor::Blocker)
TxConsequences
    consequences_helper(PreflightContext const& ctx)
{
    return TxConsequences(ctx.tx, TxConsequences::blocker);
};

// For Transactor::Custom
template <class T>
requires(T::ConsequencesFactory == Transactor::Custom)
TxConsequences
    consequences_helper(PreflightContext const& ctx)
{
    return T::makeTxConsequences(ctx);
};
// clang-format on

static std::pair<NotTEC, TxConsequences>
invoke_preflight(PreflightContext const& ctx)
{
    try
    {
        return with_txn_type(ctx.tx.getTxnType(), [&]<typename T>() {
            auto const tec = T::preflight(ctx);
            return std::make_pair(
                tec,
                isTesSuccess(tec) ? consequences_helper<T>(ctx)
                                  : TxConsequences{tec});
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        JLOG(ctx.j.fatal())
            << "Unknown transaction type in preflight: " << e.txnType;
        assert(false);
        return {temUNKNOWN, TxConsequences{temUNKNOWN}};
    }
}

static TER
invoke_preclaim(PreclaimContext const& ctx)
{
    try
    {
        // use name hiding to accomplish compile-time polymorphism of static
        // class functions for Transactor and derived classes.
        return with_txn_type(ctx.tx.getTxnType(), [&]<typename T>() {
            // If the transactor requires a valid account and the transaction
            // doesn't list one, preflight will have already a flagged a
            // failure.
            auto const id = ctx.tx.getAccountID(sfAccount);

            if (id != beast::zero)
            {
                TER result = T::checkSeqProxy(ctx.view, ctx.tx, ctx.j);

                if (result != tesSUCCESS)
                    return result;

                result = T::checkPriorTxAndLastLedger(ctx);

                if (result != tesSUCCESS)
                    return result;

                result = T::checkFee(ctx, calculateBaseFee(ctx.view, ctx.tx));

                if (result != tesSUCCESS)
                    return result;

                result = T::checkSign(ctx);

                if (result != tesSUCCESS)
                    return result;
            }

            return T::preclaim(ctx);
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        JLOG(ctx.j.fatal())
            << "Unknown transaction type in preclaim: " << e.txnType;
        assert(false);
        return temUNKNOWN;
    }
}

static XRPAmount
invoke_calculateBaseFee(ReadView const& view, STTx const& tx)
{
    try
    {
        return with_txn_type(tx.getTxnType(), [&]<typename T>() {
            return T::calculateBaseFee(view, tx);
        });
    }
    catch (UnknownTxnType const& e)
    {
        assert(false);
        return XRPAmount{0};
    }
}

TxConsequences::TxConsequences(NotTEC pfresult)
    : isBlocker_(false)
    , fee_(beast::zero)
    , potentialSpend_(beast::zero)
    , seqProx_(SeqProxy::sequence(0))
    , sequencesConsumed_(0)
{
    assert(!isTesSuccess(pfresult));
}

TxConsequences::TxConsequences(STTx const& tx)
    : isBlocker_(false)
    , fee_(
          tx[sfFee].native() && !tx[sfFee].negative() ? tx[sfFee].xrp()
                                                      : beast::zero)
    , potentialSpend_(beast::zero)
    , seqProx_(tx.getSeqProxy())
    , sequencesConsumed_(tx.getSeqProxy().isSeq() ? 1 : 0)
{
}

TxConsequences::TxConsequences(STTx const& tx, Category category)
    : TxConsequences(tx)
{
    isBlocker_ = (category == blocker);
}

TxConsequences::TxConsequences(STTx const& tx, XRPAmount potentialSpend)
    : TxConsequences(tx)
{
    potentialSpend_ = potentialSpend;
}

TxConsequences::TxConsequences(STTx const& tx, std::uint32_t sequencesConsumed)
    : TxConsequences(tx)
{
    sequencesConsumed_ = sequencesConsumed;
}

static std::pair<TER, bool>
invoke_apply(ApplyContext& ctx)
{
    try
    {
        return with_txn_type(ctx.tx.getTxnType(), [&]<typename T>() {
            T p(ctx);
            return p();
        });
    }
    catch (UnknownTxnType const& e)
    {
        // Should never happen
        JLOG(ctx.journal.fatal())
            << "Unknown transaction type in apply: " << e.txnType;
        assert(false);
        return {temUNKNOWN, false};
    }
}

PreflightResult
preflight(
    Application& app,
    Rules const& rules,
    STTx const& tx,
    ApplyFlags flags,
    beast::Journal j)
{
    PreflightContext const pfctx(app, tx, rules, flags, j);
    try
    {
        return {pfctx, invoke_preflight(pfctx)};
    }
    catch (std::exception const& e)
    {
        JLOG(j.fatal()) << "apply: " << e.what();
        return {pfctx, {tefEXCEPTION, TxConsequences{tx}}};
    }
}

PreclaimResult
preclaim(
    PreflightResult const& preflightResult,
    Application& app,
    OpenView const& view)
{
    std::optional<PreclaimContext const> ctx;
    if (preflightResult.rules != view.rules())
    {
        auto secondFlight = preflight(
            app,
            view.rules(),
            preflightResult.tx,
            preflightResult.flags,
            preflightResult.j);
        ctx.emplace(
            app,
            view,
            secondFlight.ter,
            secondFlight.tx,
            secondFlight.flags,
            secondFlight.j);
    }
    else
    {
        ctx.emplace(
            app,
            view,
            preflightResult.ter,
            preflightResult.tx,
            preflightResult.flags,
            preflightResult.j);
    }
    try
    {
        if (ctx->preflightResult != tesSUCCESS)
            return {*ctx, ctx->preflightResult};
        return {*ctx, invoke_preclaim(*ctx)};
    }
    catch (std::exception const& e)
    {
        JLOG(ctx->j.fatal()) << "apply: " << e.what();
        return {*ctx, tefEXCEPTION};
    }
}

XRPAmount
calculateBaseFee(ReadView const& view, STTx const& tx)
{
    return invoke_calculateBaseFee(view, tx);
}

XRPAmount
calculateDefaultBaseFee(ReadView const& view, STTx const& tx)
{
    return Transactor::calculateBaseFee(view, tx);
}

std::pair<TER, bool>
doApply(PreclaimResult const& preclaimResult, Application& app, OpenView& view)
{
    if (preclaimResult.view.seq() != view.seq())
    {
        // Logic error from the caller. Don't have enough
        // info to recover.
        return {tefEXCEPTION, false};
    }
    try
    {
        if (!preclaimResult.likelyToClaimFee)
            return {preclaimResult.ter, false};
        ApplyContext ctx(
            app,
            view,
            preclaimResult.tx,
            preclaimResult.ter,
            calculateBaseFee(view, preclaimResult.tx),
            preclaimResult.flags,
            preclaimResult.j);
        return invoke_apply(ctx);
    }
    catch (std::exception const& e)
    {
        JLOG(preclaimResult.j.fatal()) << "apply: " << e.what();
        return {tefEXCEPTION, false};
    }
}

}  // namespace ripple
