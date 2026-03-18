#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/conditions/Condition.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/transactors/escrow/EscrowCreate.h>

namespace xrpl {

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
    auto const amount = ctx.tx[sfAmount];
    return TxConsequences{ctx.tx, isXRP(amount) ? amount.xrp() : beast::zero};
}

template <ValidIssueType T>
static NotTEC
escrowCreatePreflightHelper(PreflightContext const& ctx);

template <>
NotTEC
escrowCreatePreflightHelper<Issue>(PreflightContext const& ctx)
{
    STAmount const amount = ctx.tx[sfAmount];
    if (amount.native() || amount <= beast::zero)
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
    if (amount.native() || amount.mpt() > MPTAmount{maxMPTokenAmount} || amount <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

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
        if (amount <= beast::zero)
            return temBAD_AMOUNT;
    }

    // We must specify at least one timeout value
    if (!ctx.tx[~sfCancelAfter] && !ctx.tx[~sfFinishAfter])
        return temBAD_EXPIRATION;

    // If both finish and cancel times are specified then the cancel time must
    // be strictly after the finish time.
    if (ctx.tx[~sfCancelAfter] && ctx.tx[~sfFinishAfter] &&
        ctx.tx[sfCancelAfter] <= ctx.tx[sfFinishAfter])
        return temBAD_EXPIRATION;

    // In the absence of a FinishAfter, the escrow can be finished
    // immediately, which can be confusing. When creating an escrow,
    // we want to ensure that either a FinishAfter time is explicitly
    // specified or a completion condition is attached.
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

    // If the lsfAllowTrustLineLocking is not enabled, return tecNO_PERMISSION
    auto const sleIssuer = ctx.view.read(keylet::account(issuer));
    if (!sleIssuer)
        return tecNO_ISSUER;
    if (!sleIssuer->isFlag(lsfAllowTrustLineLocking))
        return tecNO_PERMISSION;

    // If the account does not have a trustline to the issuer, return tecNO_LINE
    auto const sleRippleState = ctx.view.read(keylet::line(account, issuer, amount.getCurrency()));
    if (!sleRippleState)
        return tecNO_LINE;

    STAmount const balance = (*sleRippleState)[sfBalance];

    // If balance is positive, issuer must have higher address than account
    if (balance > beast::zero && issuer < account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If balance is negative, issuer must have lower address than account
    if (balance < beast::zero && issuer > account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If the issuer has requireAuth set, check if the account is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), account); !isTesSuccess(ter))
        return ter;

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(ctx.view, amount.issue(), dest); !isTesSuccess(ter))
        return ter;

    // If the issuer has frozen the account, return tecFROZEN
    if (isFrozen(ctx.view, account, amount.issue()))
        return tecFROZEN;

    // If the issuer has frozen the destination, return tecFROZEN
    if (isFrozen(ctx.view, dest, amount.issue()))
        return tecFROZEN;

    STAmount const spendableAmount =
        accountHolds(ctx.view, account, amount.getCurrency(), issuer, fhIGNORE_FREEZE, ctx.j);

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
    auto const issuanceKey = keylet::mptIssuance(amount.get<MPTIssue>().getMptID());
    auto const sleIssuance = ctx.view.read(issuanceKey);
    if (!sleIssuance)
        return tecOBJECT_NOT_FOUND;

    // If the lsfMPTCanEscrow is not enabled, return tecNO_PERMISSION
    if (!sleIssuance->isFlag(lsfMPTCanEscrow))
        return tecNO_PERMISSION;

    // If the issuer is not the same as the issuer of the mpt, return
    // tecNO_PERMISSION
    if (sleIssuance->getAccountID(sfIssuer) != issuer)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If the account does not have the mpt, return tecOBJECT_NOT_FOUND
    if (!ctx.view.exists(keylet::mptoken(issuanceKey.key, account)))
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter = requireAuth(ctx.view, mptIssue, account, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    // If the issuer has requireAuth set, check if the destination is
    // authorized
    if (auto const ter = requireAuth(ctx.view, mptIssue, dest, AuthType::WeakAuth);
        !isTesSuccess(ter))
        return ter;

    // If the issuer has frozen the account, return tecLOCKED
    if (isFrozen(ctx.view, account, mptIssue))
        return tecLOCKED;

    // If the issuer has frozen the destination, return tecLOCKED
    if (isFrozen(ctx.view, dest, mptIssue))
        return tecLOCKED;

    // If the mpt cannot be transferred, return tecNO_AUTH
    if (auto const ter = canTransfer(ctx.view, mptIssue, account, dest); !isTesSuccess(ter))
        return ter;

    STAmount const spendableAmount = accountHolds(
        ctx.view, account, amount.get<MPTIssue>(), fhIGNORE_FREEZE, ahIGNORE_AUTH, ctx.j);

    // If the balance is less than or equal to 0, return tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::zero)
        return tecINSUFFICIENT_FUNDS;

    // If the spendable amount is less than the amount, return
    // tecINSUFFICIENT_FUNDS
    if (spendableAmount < amount)
        return tecINSUFFICIENT_FUNDS;

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
    if (issuer == sender)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ter = rippleCredit(
        view, sender, issuer, amount, amount.holds<MPTIssue>() ? false : true, journal);
    if (!isTesSuccess(ter))
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
    if (issuer == sender)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ter = rippleLockEscrowMPT(view, sender, amount, journal);
    if (!isTesSuccess(ter))
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

TER
EscrowCreate::doApply()
{
    auto const closeTime = ctx_.view().header().parentCloseTime;

    if (ctx_.tx[~sfCancelAfter] && after(closeTime, ctx_.tx[sfCancelAfter]))
        return tecNO_PERMISSION;

    if (ctx_.tx[~sfFinishAfter] && after(closeTime, ctx_.tx[sfFinishAfter]))
        return tecNO_PERMISSION;

    auto const sle = ctx_.view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check reserve and funds availability
    STAmount const amount{ctx_.tx[sfAmount]};

    auto const reserve = ctx_.view().fees().accountReserve((*sle)[sfOwnerCount] + 1);

    auto const balance = sle->getFieldAmount(sfBalance).xrp();
    if (balance < reserve)
        return tecINSUFFICIENT_RESERVE;

    // Check reserve and funds availability
    if (isXRP(amount))
    {
        if (balance < reserve + STAmount(amount).xrp())
            return tecUNFUNDED;
    }

    // Check destination account
    {
        auto const sled = ctx_.view().read(keylet::account(ctx_.tx[sfDestination]));
        if (!sled)
            return tecNO_DST;  // LCOV_EXCL_LINE
        if (((*sled)[sfFlags] & lsfRequireDestTag) && !ctx_.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;
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

    if (ctx_.view().rules().enabled(fixIncludeKeyletFields))
    {
        (*slep)[sfSequence] = ctx_.tx.getSeqValue();
    }

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
            keylet::ownerDir(account_), escrowKeylet, describeOwnerDir(account_));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*slep)[sfOwnerNode] = *page;
    }

    // If it's not a self-send, add escrow to recipient's owner directory.
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

    // Deduct owner's balance
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

    // increment owner count
    adjustOwnerCount(ctx_.view(), sle, 1, ctx_.journal);
    ctx_.view().update(sle);
    return tesSUCCESS;
}

}  // namespace xrpl
