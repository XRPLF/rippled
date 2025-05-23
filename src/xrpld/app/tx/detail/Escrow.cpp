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

#include <xrpld/app/misc/CredentialHelpers.h>
#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/tx/detail/Escrow.h>
#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/conditions/Condition.h>
#include <xrpld/conditions/Fulfillment.h>
#include <xrpld/ledger/ApplyView.h>
#include <xrpld/ledger/View.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

// During an EscrowFinish, the transaction must specify both
// a condition and a fulfillment. We track whether that
// fulfillment matches and validates the condition.
#define SF_CF_INVALID SF_PRIVATE5
#define SF_CF_VALID SF_PRIVATE6

namespace ripple {

/*
    Escrow
    ======

    Escrow is a feature of the XRP Ledger that allows you to send conditional
    XRP payments. These conditional payments, called escrows, set aside XRP and
    deliver it later when certain conditions are met. Conditions to successfully
    finish an escrow include time-based unlocks and crypto-conditions. Escrows
    can also be set to expire if not finished in time.

    The XRP set aside in an escrow is locked up. No one can use or destroy the
    XRP until the escrow has been successfully finished or canceled. Before the
    expiration time, only the intended receiver can get the XRP. After the
    expiration time, the XRP can only be returned to the sender.

    For more details on escrow, including examples, diagrams and more please
    visit https://xrpl.org/escrow.html

    For details on specific transactions, including fields and validation rules
    please see:

    `EscrowCreate`
    --------------
        See: https://xrpl.org/escrowcreate.html

    `EscrowFinish`
    --------------
        See: https://xrpl.org/escrowfinish.html

    `EscrowCancel`
    --------------
        See: https://xrpl.org/escrowcancel.html
*/

//------------------------------------------------------------------------------

TxConsequences
EscrowCreate::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{
        ctx.tx, isXRP(ctx.tx[sfAmount]) ? ctx.tx[sfAmount].xrp() : beast::zero};
}

template <ValidIssueType T>
static NotTEC
escrowCreatePreflightHelper(PreflightContext const& ctx);

template <>
NotTEC
escrowCreatePreflightHelper<Issue>(PreflightContext const& ctx)
{
    STAmount const amount = ctx.tx[sfAmount];
    if (!isLegalNet(amount) || amount <= beast::zero)
        return temBAD_AMOUNT;

    if (badCurrency() == amount.getCurrency())
        return temBAD_CURRENCY;

    return tesSUCCESS;
}

template <>
NotTEC
escrowCreatePreflightHelper<MPTIssue>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    auto const amount = ctx.tx[sfAmount];
    if (amount.mpt() > MPTAmount{maxMPTokenAmount} || amount <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

NotTEC
EscrowCreate::preflight(PreflightContext const& ctx)
{
    if (ctx.rules.enabled(fix1543) && ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    STAmount const amount{ctx.tx[sfAmount]};
    if (!isXRP(amount))
    {
        if (!ctx.rules.enabled(featureTokenEscrow))
            return temDISABLED;

        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowCreatePreflightHelper<T>(ctx);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }

    if (amount <= beast::zero)
        return temBAD_AMOUNT;

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

    if (auto const cb = ctx.tx[~sfCondition])
    {
        using namespace ripple::cryptoconditions;

        std::error_code ec;

        auto condition = Condition::deserialize(*cb, ec);
        if (!condition)
        {
            JLOG(ctx.j.debug())
                << "Malformed condition during escrow creation: "
                << ec.message();
            return temMALFORMED;
        }

        // Conditions other than PrefixSha256 require the
        // "CryptoConditionsSuite" amendment:
        if (condition->type != Type::preimageSha256 &&
            !ctx.rules.enabled(featureCryptoConditionsSuite))
            return temDISABLED;
    }

    return preflight2(ctx);
}

template <ValidIssueType T>
static TER
escrowCreatePreclaimHelper(
    PreclaimContext const& ctx,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount);

template <>
TER
escrowCreatePreclaimHelper<Issue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecNO_PERMISSION
    if (issuer == account)
        return tecNO_PERMISSION;

    // If the lsfAllowTokenEscrow is not enabled, return tecNO_PERMISSION
    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    if (!sleIssuer)
        return tecNO_ISSUER;
    if (!sleIssuer->isFlag(lsfAllowTokenEscrow))
        return tecNO_PERMISSION;

    // If the account does not have a trustline to the issuer, return tecNO_LINE
    auto const sleRippleState =
        ctx.view.read(keylet::line(account, issuer, amount.getCurrency()));
    if (!sleRippleState)
        return tecNO_LINE;

    STAmount const balance = (*sleRippleState)[sfBalance];

    // If balance is positive, issuer must have higher address than account
    if (balance > beast::zero && issuer < account)
        return tecNO_PERMISSION;

    // If balance is negative, issuer must have lower address than account
    if (balance < beast::zero && issuer > account)
        return tecNO_PERMISSION;

    // If the issuer has requireAuth set, check if the account is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), account);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), dest);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has frozen the account, return tecFROZEN
    if (isFrozen(ctx.view, account, amount.issue()))
        return tecFROZEN;

    // If the issuer has frozen the destination, return tecFROZEN
    if (isFrozen(ctx.view, dest, amount.issue()))
        return tecFROZEN;

    STAmount const spendableAmount = accountHolds(
        ctx.view,
        account,
        amount.getCurrency(),
        issuer,
        fhIGNORE_FREEZE,
        ctx.j);

    // If the balance is less than or equal to 0, return tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::zero)
        return tecINSUFFICIENT_FUNDS;

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
        return tecINSUFFICIENT_FUNDS;

    // If the amount is not addable to the balance, return tecPRECISION_LOSS
    if (!canAdd(spendableAmount, amount))
        return tecPRECISION_LOSS;

    return tesSUCCESS;
}

template <>
TER
escrowCreatePreclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecNO_PERMISSION
    if (issuer == account)
        return tecNO_PERMISSION;

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey =
        keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // If the lsfMPTCanEscrow is not enabled, return tecNO_PERMISSION
    if (!sleIssuance->isFlag(lsfMPTCanEscrow))
        return tecNO_PERMISSION;

    // If the issuer is not the same as the issuer of the mpt, return
    // tecNO_PERMISSION
    if (sleIssuance->getAccountID(sfIssuer) != issuer)
        return tecNO_PERMISSION;

    // If the account does not have the mpt, return tecOBJECT_NOT_FOUND
    if (!ctx.view.exists(keylet::mptoken(issuanceKey.key, account)))
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter =
            requireAuth(ctx.view, mptIssue, account, MPTAuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has requireAuth set, check if the destination is
    // authorized
    if (auto const ter =
            requireAuth(ctx.view, mptIssue, dest, MPTAuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has frozen the account, return tecFROZEN
    if (isFrozen(ctx.view, account, mptIssue))
        return tecFROZEN;

    // If the issuer has frozen the destination, return tecFROZEN
    if (isFrozen(ctx.view, dest, mptIssue))
        return tecFROZEN;

    // If the mpt cannot be transferred, return tecNO_AUTH
    if (auto const ter = canTransfer(ctx.view, mptIssue, account, dest);
        ter != tesSUCCESS)
        return ter;

    STAmount const spendableAmount = accountHolds(
        ctx.view,
        account,
        amount.get<MPTIssue>(),
        fhIGNORE_FREEZE,
        ahIGNORE_AUTH,
        ctx.j);

    // If the balance is less than or equal to 0, return tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::zero)
        return tecINSUFFICIENT_FUNDS;

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
        return tecINSUFFICIENT_FUNDS;

    // If the amount is not addable to the balance, return tecPRECISION_LOSS
    if (!canAdd(spendableAmount, amount))
        return tecPRECISION_LOSS;

    return tesSUCCESS;
}

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
            return temDISABLED;

        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowCreatePreclaimHelper<T>(
                        ctx, account, dest, amount);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }
    return tesSUCCESS;
}

template <ValidIssueType T>
static TER
escrowLockApplyHelper(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal);

template <>
TER
escrowLockApplyHelper<Issue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal)
{
    // Defensive: Issuer cannot create an escrow
    // LCOV_EXCL_START
    if (issuer == sender)
        return tecINTERNAL;
    // LCOV_EXCL_STOP

    auto const ter = rippleCredit(
        view,
        sender,
        issuer,
        amount,
        amount.holds<MPTIssue>() ? false : true,
        journal);
    if (ter != tesSUCCESS)
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

template <>
TER
escrowLockApplyHelper<MPTIssue>(
    ApplyView& view,
    AccountID const& issuer,
    AccountID const& sender,
    STAmount const& amount,
    beast::Journal journal)
{
    // Defensive: Issuer cannot create an escrow
    // LCOV_EXCL_START
    if (issuer == sender)
        return tecINTERNAL;
    // LCOV_EXCL_STOP

    auto const ter = rippleLockEscrowMPT(view, sender, amount, journal);
    if (ter != tesSUCCESS)
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

TER
EscrowCreate::doApply()
{
    auto const closeTime = ctx_.view().info().parentCloseTime;

    // Prior to fix1571, the cancel and finish times could be greater
    // than or equal to the parent ledgers' close time.
    //
    // With fix1571, we require that they both be strictly greater
    // than the parent ledgers' close time.
    if (ctx_.view().rules().enabled(fix1571))
    {
        if (ctx_.tx[~sfCancelAfter] && after(closeTime, ctx_.tx[sfCancelAfter]))
            return tecNO_PERMISSION;

        if (ctx_.tx[~sfFinishAfter] && after(closeTime, ctx_.tx[sfFinishAfter]))
            return tecNO_PERMISSION;
    }
    else
    {
        if (ctx_.tx[~sfCancelAfter])
        {
            auto const cancelAfter = ctx_.tx[sfCancelAfter];

            if (closeTime.time_since_epoch().count() >= cancelAfter)
                return tecNO_PERMISSION;
        }

        if (ctx_.tx[~sfFinishAfter])
        {
            auto const finishAfter = ctx_.tx[sfFinishAfter];

            if (closeTime.time_since_epoch().count() >= finishAfter)
                return tecNO_PERMISSION;
        }
    }

    auto const sle = ctx_.view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;

    // Check reserve and funds availability
    STAmount const amount{ctx_.tx[sfAmount]};

    auto const reserve =
        ctx_.view().fees().accountReserve((*sle)[sfOwnerCount] + 1);

    if (mSourceBalance < reserve)
        return tecINSUFFICIENT_RESERVE;

    // Check reserve and funds availability
    if (isXRP(amount))
    {
        if (mSourceBalance < reserve + STAmount(amount).xrp())
            return tecUNFUNDED;
    }

    // Check destination account
    {
        auto const sled =
            ctx_.view().read(keylet::account(ctx_.tx[sfDestination]));
        if (!sled)
            return tecNO_DST;
        if (((*sled)[sfFlags] & lsfRequireDestTag) &&
            !ctx_.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;

        // Obeying the lsfDissalowXRP flag was a bug.  Piggyback on
        // featureDepositAuth to remove the bug.
        if (!ctx_.view().rules().enabled(featureDepositAuth) &&
            ((*sled)[sfFlags] & lsfDisallowXRP))
            return tecNO_TARGET;
    }

    // Create escrow in ledger.  Note that we we use the value from the
    // sequence or ticket.  For more explanation see comments in SeqProxy.h.
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

    if (ctx_.view().rules().enabled(featureTokenEscrow) && !isXRP(amount))
    {
        auto const xferRate = transferRate(ctx_.view(), amount);
        if (xferRate != parityRate)
            (*slep)[sfTransferRate] = xferRate.value;
    }

    ctx_.view().insert(slep);

    // Add escrow to sender's owner directory
    {
        auto page = ctx_.view().dirInsert(
            keylet::ownerDir(account_),
            escrowKeylet,
            describeOwnerDir(account_));
        if (!page)
            return tecDIR_FULL;
        (*slep)[sfOwnerNode] = *page;
    }

    // If it's not a self-send, add escrow to recipient's owner directory.
    AccountID const dest = ctx_.tx[sfDestination];
    if (dest != account_)
    {
        auto page = ctx_.view().dirInsert(
            keylet::ownerDir(dest), escrowKeylet, describeOwnerDir(dest));
        if (!page)
            return tecDIR_FULL;
        (*slep)[sfDestinationNode] = *page;
    }

    // IOU escrow objects are added to the issuer's owner directory to help
    // track the total locked balance. For MPT, this isn't necessary because the
    // locked balance is already stored directly in the MPTokenIssuance object.
    AccountID const issuer = amount.getIssuer();
    if (!isXRP(amount) && issuer != account_ && issuer != dest &&
        !amount.holds<MPTIssue>())
    {
        auto page = ctx_.view().dirInsert(
            keylet::ownerDir(issuer), escrowKeylet, describeOwnerDir(issuer));
        if (!page)
            return tecDIR_FULL;
        (*slep)[sfIssuerNode] = *page;
    }

    // Deduct owner's balance
    if (isXRP(amount))
        (*sle)[sfBalance] = (*sle)[sfBalance] - amount;
    else
    {
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowLockApplyHelper<T>(
                        ctx_.view(), issuer, account_, amount, j_);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }

    // increment owner count
    adjustOwnerCount(ctx_.view(), sle, 1, ctx_.journal);
    ctx_.view().update(sle);
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

static bool
checkCondition(Slice f, Slice c)
{
    using namespace ripple::cryptoconditions;

    std::error_code ec;

    auto condition = Condition::deserialize(c, ec);
    if (!condition)
        return false;

    auto fulfillment = Fulfillment::deserialize(f, ec);
    if (!fulfillment)
        return false;

    return validate(*fulfillment, *condition);
}

NotTEC
EscrowFinish::preflight(PreflightContext const& ctx)
{
    if (ctx.rules.enabled(fix1543) && ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (ctx.tx.isFieldPresent(sfCredentialIDs) &&
        !ctx.rules.enabled(featureCredentials))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    auto const cb = ctx.tx[~sfCondition];
    auto const fb = ctx.tx[~sfFulfillment];

    // If you specify a condition, then you must also specify
    // a fulfillment.
    if (static_cast<bool>(cb) != static_cast<bool>(fb))
        return temMALFORMED;

    // Verify the transaction signature. If it doesn't work
    // then don't do any more work.
    {
        auto const ret = preflight2(ctx);
        if (!isTesSuccess(ret))
            return ret;
    }

    if (cb && fb)
    {
        auto& router = ctx.app.getHashRouter();

        auto const id = ctx.tx.getTransactionID();
        auto const flags = router.getFlags(id);

        // If we haven't checked the condition, check it
        // now. Whether it passes or not isn't important
        // in preflight.
        if (!(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            if (checkCondition(*fb, *cb))
                router.setFlags(id, SF_CF_VALID);
            else
                router.setFlags(id, SF_CF_INVALID);
        }
    }

    if (auto const err = credentials::checkFields(ctx); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

XRPAmount
EscrowFinish::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount extraFee{0};

    if (auto const fb = tx[~sfFulfillment])
    {
        extraFee += view.fees().base * (32 + (fb->size() / 16));
    }

    return Transactor::calculateBaseFee(view, tx) + extraFee;
}

template <ValidIssueType T>
static TER
escrowFinishPreclaimHelper(
    PreclaimContext const& ctx,
    AccountID const& dest,
    STAmount const& amount);

template <>
TER
escrowFinishPreclaimHelper<Issue>(
    PreclaimContext const& ctx,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tesSUCCESS
    if (issuer == dest)
        return tesSUCCESS;

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), dest);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has deep frozen the destination, return tecFROZEN
    if (isDeepFrozen(ctx.view, dest, amount.getCurrency(), amount.getIssuer()))
        return tecFROZEN;

    return tesSUCCESS;
}

template <>
TER
escrowFinishPreclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    AccountID const& dest,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the dest, return tesSUCCESS
    if (issuer == dest)
        return tesSUCCESS;

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey =
        keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the destination is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter =
            requireAuth(ctx.view, mptIssue, dest, MPTAuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has frozen the destination, return tecFROZEN
    if (isFrozen(ctx.view, dest, mptIssue))
        return tecFROZEN;

    return tesSUCCESS;
}

TER
EscrowFinish::preclaim(PreclaimContext const& ctx)
{
    if (ctx.view.rules().enabled(featureCredentials))
    {
        if (auto const err = credentials::valid(ctx, ctx.tx[sfAccount]);
            !isTesSuccess(err))
            return err;
    }

    auto const k = keylet::escrow(ctx.tx[sfOwner], ctx.tx[sfOfferSequence]);
    auto const slep = ctx.view.read(k);
    if (!slep)
        return tecNO_TARGET;

    AccountID const dest = (*slep)[sfDestination];
    STAmount const amount = (*slep)[sfAmount];

    if (!isXRP(amount))
    {
        if (!ctx.view.rules().enabled(featureTokenEscrow))
            return temDISABLED;

        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowFinishPreclaimHelper<T>(ctx, dest, amount);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }
    return tesSUCCESS;
}

template <ValidIssueType T>
static TER
escrowUnlockApplyHelper(
    ApplyView& view,
    Rate lockedRate,
    std::shared_ptr<SLE> const& sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal);

template <>
TER
escrowUnlockApplyHelper<Issue>(
    ApplyView& view,
    Rate lockedRate,
    std::shared_ptr<SLE> const& sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal)
{
    Keylet const trustLineKey = keylet::line(receiver, amount.issue());
    bool const recvLow = issuer > receiver;
    bool const senderIssuer = issuer == sender;
    bool const receiverIssuer = issuer == receiver;

    // LCOV_EXCL_START
    if (senderIssuer)
        return tecINTERNAL;
    // LCOV_EXCL_STOP

    if (receiverIssuer)
        return tesSUCCESS;

    // Review Note: We could remove this and just say to use batch to auth the
    // token first
    if (!view.exists(trustLineKey) && createAsset && !receiverIssuer)
    {
        // Can the account cover the trust line's reserve?
        if (std::uint32_t const ownerCount = {sleDest->at(sfOwnerCount)};
            xrpBalance < view.fees().accountReserve(ownerCount + 1))
        {
            JLOG(journal.trace()) << "Trust line does not exist. "
                                     "Insufficent reserve to create line.";

            return tecNO_LINE_INSUF_RESERVE;
        }

        Currency const currency = amount.getCurrency();
        STAmount initialBalance(amount.issue());
        initialBalance.setIssuer(noAccount());

        // clang-format off
        if (TER const ter = trustCreate(
                view,                            // payment sandbox
                recvLow,                        // is dest low?
                issuer,                         // source
                receiver,                           // destination
                trustLineKey.key,               // ledger index
                sleDest,                        // Account to add to
                false,                          // authorize account
                (sleDest->getFlags() & lsfDefaultRipple) == 0,
                false,                          // freeze trust line
                false,                          // deep freeze trust line
                initialBalance,                 // zero initial balance
                Issue(currency, receiver),   // limit of zero
                0,                              // quality in
                0,                              // quality out
                journal);                       // journal
            !isTesSuccess(ter))
        {
            return ter;
        }
        // clang-format on

        view.update(sleDest);
    }

    if (!view.exists(trustLineKey) && !receiverIssuer)
        return tecNO_LINE;

    auto const xferRate = transferRate(view, amount);
    // update if issuer rate is less than locked rate
    if (xferRate < lockedRate)
        lockedRate = xferRate;

    // Transfer Rate only applies when:
    // 1. Issuer is not involved in the transfer (senderIssuer or
    // receiverIssuer)
    // 2. The locked rate is different from the parity rate

    // NOTE: Transfer fee in escrow works a bit differently from a normal
    // payment. In escrow, the fee is deducted from the escrowed/sending amount,
    // whereas in a normal payment, the transfer fee is taken on top of the
    // sending amount.
    auto finalAmt = amount;
    if ((!senderIssuer && !receiverIssuer) && lockedRate != parityRate)
    {
        // compute transfer fee, if any
        auto const xferFee = amount.value() -
            divideRound(amount, lockedRate, amount.issue(), true);
        // compute balance to transfer
        finalAmt = amount.value() - xferFee;
    }

    // If destination is not the issuer then transfer funds
    if (!receiverIssuer)
    {
        auto const ter =
            rippleCredit(view, issuer, receiver, finalAmt, true, journal);
        if (ter != tesSUCCESS)
            return ter;  // LCOV_EXCL_LINE
    }
    return tesSUCCESS;
}

template <>
TER
escrowUnlockApplyHelper<MPTIssue>(
    ApplyView& view,
    Rate lockedRate,
    std::shared_ptr<SLE> const& sleDest,
    STAmount const& xrpBalance,
    STAmount const& amount,
    AccountID const& issuer,
    AccountID const& sender,
    AccountID const& receiver,
    bool createAsset,
    beast::Journal journal)
{
    bool const senderIssuer = issuer == sender;
    bool const receiverIssuer = issuer == receiver;

    auto const mptID = amount.get<MPTIssue>().getMptID();
    auto const issuanceKey = keylet::mptIssuance(mptID);
    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)) &&
        createAsset && !receiverIssuer)
    {
        if (std::uint32_t const ownerCount = {sleDest->at(sfOwnerCount)};
            xrpBalance < view.fees().accountReserve(ownerCount + 1))
        {
            return tecINSUFFICIENT_RESERVE;
        }

        if (auto const ter =
                MPTokenAuthorize::createMPToken(view, mptID, receiver, 0);
            !isTesSuccess(ter))
        {
            return ter;
        }

        // Update owner count.
        adjustOwnerCount(view, sleDest, 1, journal);
    }

    if (!view.exists(keylet::mptoken(issuanceKey.key, receiver)) &&
        !receiverIssuer)
        return tecNO_PERMISSION;

    auto const xferRate = transferRate(view, amount);
    // update if issuer rate is less than locked rate
    if (xferRate < lockedRate)
        lockedRate = xferRate;

    // Transfer Rate only applies when:
    // 1. Issuer is not involved in the transfer (senderIssuer or
    // receiverIssuer)
    // 2. The locked rate is different from the parity rate

    // NOTE: Transfer fee in escrow works a bit differently from a normal
    // payment. In escrow, the fee is deducted from the escrowed/sending amount,
    // whereas in a normal payment, the transfer fee is taken on top of the
    // sending amount.
    auto finalAmt = amount;
    if ((!senderIssuer && !receiverIssuer) && lockedRate != parityRate)
    {
        // compute transfer fee, if any
        auto const xferFee = amount.value() -
            divideRound(amount, lockedRate, amount.asset(), true);
        // compute balance to transfer
        finalAmt = amount.value() - xferFee;
    }

    return rippleUnlockEscrowMPT(view, sender, receiver, finalAmt, journal);
}

TER
EscrowFinish::doApply()
{
    auto const k = keylet::escrow(ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);
    auto const slep = ctx_.view().peek(k);
    if (!slep)
        return tecNO_TARGET;

    // If a cancel time is present, a finish operation should only succeed prior
    // to that time. fix1571 corrects a logic error in the check that would make
    // a finish only succeed strictly after the cancel time.
    if (ctx_.view().rules().enabled(fix1571))
    {
        auto const now = ctx_.view().info().parentCloseTime;

        // Too soon: can't execute before the finish time
        if ((*slep)[~sfFinishAfter] && !after(now, (*slep)[sfFinishAfter]))
            return tecNO_PERMISSION;

        // Too late: can't execute after the cancel time
        if ((*slep)[~sfCancelAfter] && after(now, (*slep)[sfCancelAfter]))
            return tecNO_PERMISSION;
    }
    else
    {
        // Too soon?
        if ((*slep)[~sfFinishAfter] &&
            ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
                (*slep)[sfFinishAfter])
            return tecNO_PERMISSION;

        // Too late?
        if ((*slep)[~sfCancelAfter] &&
            ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
                (*slep)[sfCancelAfter])
            return tecNO_PERMISSION;
    }

    // Check cryptocondition fulfillment
    {
        auto const id = ctx_.tx.getTransactionID();
        auto flags = ctx_.app.getHashRouter().getFlags(id);

        auto const cb = ctx_.tx[~sfCondition];

        // It's unlikely that the results of the check will
        // expire from the hash router, but if it happens,
        // simply re-run the check.
        if (cb && !(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            auto const fb = ctx_.tx[~sfFulfillment];

            if (!fb)
                return tecINTERNAL;

            if (checkCondition(*fb, *cb))
                flags = SF_CF_VALID;
            else
                flags = SF_CF_INVALID;

            ctx_.app.getHashRouter().setFlags(id, flags);
        }

        // If the check failed, then simply return an error
        // and don't look at anything else.
        if (flags & SF_CF_INVALID)
            return tecCRYPTOCONDITION_ERROR;

        // Check against condition in the ledger entry:
        auto const cond = (*slep)[~sfCondition];

        // If a condition wasn't specified during creation,
        // one shouldn't be included now.
        if (!cond && cb)
            return tecCRYPTOCONDITION_ERROR;

        // If a condition was specified during creation of
        // the suspended payment, the identical condition
        // must be presented again. We don't check if the
        // fulfillment matches the condition since we did
        // that in preflight.
        if (cond && (cond != cb))
            return tecCRYPTOCONDITION_ERROR;
    }

    // NOTE: Escrow payments cannot be used to fund accounts.
    AccountID const destID = (*slep)[sfDestination];
    auto const sled = ctx_.view().peek(keylet::account(destID));
    if (!sled)
        return tecNO_DST;

    if (ctx_.view().rules().enabled(featureDepositAuth))
    {
        if (auto err = verifyDepositPreauth(ctx_, account_, destID, sled);
            !isTesSuccess(err))
            return err;
    }

    AccountID const account = (*slep)[sfAccount];

    // Remove escrow from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(account), page, k.key, true))
        {
            JLOG(j_.fatal()) << "Unable to delete Escrow from owner.";
            return tefBAD_LEDGER;
        }
    }

    // Remove escrow from recipient's owner directory, if present.
    if (auto const optPage = (*slep)[~sfDestinationNode])
    {
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(destID), *optPage, k.key, true))
        {
            JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
            return tefBAD_LEDGER;
        }
    }

    STAmount const amount = slep->getFieldAmount(sfAmount);
    // Transfer amount to destination
    if (isXRP(amount))
        (*sled)[sfBalance] = (*sled)[sfBalance] + amount;
    else
    {
        if (!ctx_.view().rules().enabled(featureTokenEscrow))
            return temDISABLED;

        Rate lockedRate = slep->isFieldPresent(sfTransferRate)
            ? ripple::Rate(slep->getFieldU32(sfTransferRate))
            : parityRate;
        auto const issuer = amount.getIssuer();
        bool const createAsset = destID == account_;
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowUnlockApplyHelper<T>(
                        ctx_.view(),
                        lockedRate,
                        sled,
                        mPriorBalance,
                        amount,
                        issuer,
                        account,
                        destID,
                        createAsset,
                        j_);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;

        // Remove escrow from issuers owner directory, if present.
        if (auto const optPage = (*slep)[~sfIssuerNode]; optPage)
        {
            if (!ctx_.view().dirRemove(
                    keylet::ownerDir(issuer), *optPage, k.key, true))
            {
                JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
                return tefBAD_LEDGER;
            }
        }
    }

    ctx_.view().update(sled);

    // Adjust source owner count
    auto const sle = ctx_.view().peek(keylet::account(account));
    adjustOwnerCount(ctx_.view(), sle, -1, ctx_.journal);
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

NotTEC
EscrowCancel::preflight(PreflightContext const& ctx)
{
    if (ctx.rules.enabled(fix1543) && ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    return preflight2(ctx);
}

template <ValidIssueType T>
static TER
escrowCancelPreclaimHelper(
    PreclaimContext const& ctx,
    AccountID const& account,
    STAmount const& amount);

template <>
TER
escrowCancelPreclaimHelper<Issue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tesSUCCESS
    if (issuer == account)
        return tesSUCCESS;

    // If the issuer has requireAuth set, check if the account is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), account);
        ter != tesSUCCESS)
        return ter;

    return tesSUCCESS;
}

template <>
TER
escrowCancelPreclaimHelper<MPTIssue>(
    PreclaimContext const& ctx,
    AccountID const& account,
    STAmount const& amount)
{
    AccountID issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tesSUCCESS
    if (issuer == account)
        return tesSUCCESS;

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey =
        keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter =
            requireAuth(ctx.view, mptIssue, account, MPTAuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    return tesSUCCESS;
}

TER
EscrowCancel::preclaim(PreclaimContext const& ctx)
{
    auto const k = keylet::escrow(ctx.tx[sfOwner], ctx.tx[sfOfferSequence]);
    auto const slep = ctx.view.read(k);
    if (!slep)
        return tecNO_TARGET;

    AccountID const account = (*slep)[sfAccount];
    STAmount const amount = (*slep)[sfAmount];

    if (!isXRP(amount))
    {
        if (!ctx.view.rules().enabled(featureTokenEscrow))
            return temDISABLED;

        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowCancelPreclaimHelper<T>(ctx, account, amount);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;
    }
    return tesSUCCESS;
}

TER
EscrowCancel::doApply()
{
    auto const k = keylet::escrow(ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);
    auto const slep = ctx_.view().peek(k);
    if (!slep)
        return tecNO_TARGET;

    if (ctx_.view().rules().enabled(fix1571))
    {
        auto const now = ctx_.view().info().parentCloseTime;

        // No cancel time specified: can't execute at all.
        if (!(*slep)[~sfCancelAfter])
            return tecNO_PERMISSION;

        // Too soon: can't execute before the cancel time.
        if (!after(now, (*slep)[sfCancelAfter]))
            return tecNO_PERMISSION;
    }
    else
    {
        // Too soon?
        if (!(*slep)[~sfCancelAfter] ||
            ctx_.view().info().parentCloseTime.time_since_epoch().count() <=
                (*slep)[sfCancelAfter])
            return tecNO_PERMISSION;
    }

    AccountID const account = (*slep)[sfAccount];

    // Remove escrow from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(account), page, k.key, true))
        {
            JLOG(j_.fatal()) << "Unable to delete Escrow from owner.";
            return tefBAD_LEDGER;
        }
    }

    // Remove escrow from recipient's owner directory, if present.
    if (auto const optPage = (*slep)[~sfDestinationNode]; optPage)
    {
        if (!ctx_.view().dirRemove(
                keylet::ownerDir((*slep)[sfDestination]),
                *optPage,
                k.key,
                true))
        {
            JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
            return tefBAD_LEDGER;
        }
    }

    auto const sle = ctx_.view().peek(keylet::account(account));
    STAmount const amount = slep->getFieldAmount(sfAmount);

    // Transfer amount back to the owner
    if (isXRP(amount))
        (*sle)[sfBalance] = (*sle)[sfBalance] + amount;
    else
    {
        if (!ctx_.view().rules().enabled(featureTokenEscrow))
            return temDISABLED;

        Rate lockedRate = slep->isFieldPresent(sfTransferRate)
            ? ripple::Rate(slep->getFieldU32(sfTransferRate))
            : parityRate;
        auto const issuer = amount.getIssuer();
        bool const createAsset = account == account_;
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowUnlockApplyHelper<T>(
                        ctx_.view(),
                        lockedRate,
                        slep,
                        mPriorBalance,
                        amount,
                        issuer,
                        account,  // sender and receiver are the same
                        account,
                        createAsset,
                        j_);
                },
                amount.asset().value());
            !isTesSuccess(ret))
            return ret;

        // Remove escrow from issuers owner directory, if present.
        if (auto const optPage = (*slep)[~sfIssuerNode]; optPage)
        {
            if (!ctx_.view().dirRemove(
                    keylet::ownerDir(issuer), *optPage, k.key, true))
            {
                JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
                return tefBAD_LEDGER;
            }
        }
    }

    adjustOwnerCount(ctx_.view(), sle, -1, ctx_.journal);
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);

    return tesSUCCESS;
}

}  // namespace ripple
