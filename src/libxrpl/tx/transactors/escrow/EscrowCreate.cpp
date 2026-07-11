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
#include <xrpl/ledger/helpers/SLEWrappers.h>
#include <xrpl/ledger/helpers/SponsorHelpers.h>
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
    return TxConsequences{ctx.tx, isXRP(amount) ? amount.xrp() : beast::kZero};
}

bool
EscrowCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    // Only require featureMPTokensV1 when the escrow amount is an MPT and
    // fixCleanup3_2_0 is active; XRP/IOU escrows are unaffected by this gate.
    if (ctx.rules.enabled(fixCleanup3_2_0) && ctx.tx[sfAmount].holds<MPTIssue>())
        return ctx.rules.enabled(featureMPTokensV1);
    return true;
}

template <ValidIssueType T>
static NotTEC
escrowCreatePreflightHelper(PreflightContext const& ctx);

template <>
NotTEC
escrowCreatePreflightHelper<Issue>(PreflightContext const& ctx)
{
    STAmount const amount = ctx.tx[sfAmount];
    if (amount.native() || amount <= beast::kZero)
        return temBAD_AMOUNT;

    if (badCurrency() == amount.get<Issue>().currency)
        return temBAD_CURRENCY;

    return tesSUCCESS;
}

template <>
NotTEC
escrowCreatePreflightHelper<MPTIssue>(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(fixCleanup3_2_0) && !ctx.rules.enabled(featureMPTokensV1))
        return temDISABLED;

    auto const amount = ctx.tx[sfAmount];
    if (amount.native() || amount.mpt() > MPTAmount{kMaxMpTokenAmount} || amount <= beast::kZero)
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
        if (amount <= beast::kZero)
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
    auto const& issue = amount.get<Issue>();
    AccountID const& issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecNO_PERMISSION
    if (issuer == account)
        return tecNO_PERMISSION;

    // If the lsfAllowTrustLineLocking is not enabled, return tecNO_PERMISSION
    AccountRootEntry<ReadView> sleIssuer{keylet::account(issuer), ctx.view};
    if (!sleIssuer)
        return tecNO_ISSUER;
    if (!sleIssuer->isFlag(lsfAllowTrustLineLocking))
        return tecNO_PERMISSION;

    // If the account does not have a trustline to the issuer, return tecNO_LINE
    RippleStateEntry<ReadView> sleRippleState{
        keylet::trustLine(account, issuer, issue.currency), ctx.view};
    if (!sleRippleState)
        return tecNO_LINE;

    STAmount const balance = (*sleRippleState)[sfBalance];

    // If balance is positive, issuer must have higher address than account
    if (balance > beast::kZero && issuer < account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If balance is negative, issuer must have lower address than account
    if (balance < beast::kZero && issuer > account)
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If the issuer has requireAuth set, check if the account is authorized
    if (auto const ter = requireAuth(ctx.view, issue, account); !isTesSuccess(ter))
        return ter;

    // If the issuer has requireAuth set, check if the destination is authorized
    if (auto const ter = requireAuth(ctx.view, issue, dest); !isTesSuccess(ter))
        return ter;

    // If the issuer has frozen the account, return tecFROZEN
    if (isFrozen(ctx.view, account, issue))
        return tecFROZEN;

    // If the issuer has frozen the destination, return tecFROZEN
    if (isFrozen(ctx.view, dest, issue))
        return tecFROZEN;

    STAmount const spendableAmount = accountHolds(
        ctx.view, account, issue.currency, issuer, FreezeHandling::IgnoreFreeze, ctx.j);

    // If the balance is less than or equal to 0, return tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::kZero)
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
    AccountID const issuer = amount.getIssuer();
    // If the issuer is the same as the account, return tecNO_PERMISSION
    if (issuer == account)
        return tecNO_PERMISSION;

    // If the mpt does not exist, return tecOBJECT_NOT_FOUND
    auto const issuanceKey = keylet::mptokenIssuance(amount.get<MPTIssue>().getMptID());
    MPTokenIssuanceEntry<ReadView> sleIssuance{issuanceKey, ctx.view};
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
        ctx.view,
        account,
        amount.get<MPTIssue>(),
        FreezeHandling::IgnoreFreeze,
        AuthHandling::IgnoreAuth,
        ctx.j);

    // If the balance is less than or equal to 0, return tecINSUFFICIENT_FUNDS
    if (spendableAmount <= beast::kZero)
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

    AccountRootEntry<ReadView> sled{keylet::account(dest), ctx.view};
    if (!sled)
        return tecNO_DST;

    // Pseudo-accounts cannot receive escrow. Note, this is not amendment-gated
    // because all writes to pseudo-account discriminator fields **are**
    // amendment gated, hence the behaviour of this check will always match the
    // currently active amendments.
    if (isPseudoAccount(sled.sle()))
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

    auto const ter =
        directSendNoFee(view, sender, issuer, amount, !amount.holds<MPTIssue>(), journal);
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

    auto const ter = lockEscrowMPT(view, sender, amount, journal);
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

    AccountRootEntry<ApplyView> sle{keylet::account(accountID_), ctx_.view()};
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check reserve and funds availability
    STAmount const amount{ctx_.tx[sfAmount]};

    auto const balance = sle->getFieldAmount(sfBalance).xrp();
    // First check: whoever is on the hook for the new owner increment
    // can cover it. When sponsored this hits the sponsor branch and
    // validates the sponsor's reserve + remaining credit. When
    // unsponsored this hits the source branch and validates the
    // source's pre-lock balance against base + (currentOC+1)*increment.
    if (auto const ret = checkReserve(
            ctx_.getApplyViewContext(), sle.sle(), balance, {.ownerCountDelta = 1}, j_);
        !isTesSuccess(ret))
        return ret;

    if (isXRP(amount))
    {
        // Second check (XRP escrow only): after locking the escrowed
        // amount, the source must still meet its own reserve floor. This is
        // always the source's own balance against the source's own reserve —
        // the sponsor's reserve was already validated above, and a sponsor
        // never covers the locked funds. We compare directly (rather than via
        // checkReserve) because that helper diverts to the sponsor's balance
        // when a sponsor is present and would ignore the source's post-lock
        // balance entirely. ownerCountDelta differs by case:
        // - sponsored:   0  — sponsor covers the new owner increment, so the
        //                source only owes reserve for its current owners.
        // - unsponsored: 1  — source owes reserve including the new increment.
        auto const sourceReserve = accountReserve(
            ctx_.view(),
            sle.sle(),
            j_,
            {.ownerCountDelta = getTxReserveSponsorID(ctx_.tx) ? 0 : 1});
        if (balance - STAmount(amount).xrp() < sourceReserve)
            return tecUNFUNDED;
    }

    // Check destination account
    {
        AccountRootEntry<ReadView> sled{keylet::account(ctx_.tx[sfDestination]), ctx_.view()};
        if (!sled)
            return tecNO_DST;  // LCOV_EXCL_LINE
        if (sled->isFlag(lsfRequireDestTag) && !ctx_.tx[~sfDestinationTag])
            return tecDST_TAG_NEEDED;
    }

    // Create escrow in ledger.  Note that we use the value from the
    // sequence or ticket.  For more explanation see comments in SeqProxy.h.
    Keylet const escrowKeylet = keylet::escrow(accountID_, ctx_.tx.getSeqValue());
    // Build with the ApplyViewContext so create() honors reserve sponsorship.
    EscrowEntry<ApplyView> slep{escrowKeylet, ctx_.getApplyViewContext()};
    slep.newSLE();
    (*slep)[sfAmount] = amount;
    (*slep)[sfAccount] = accountID_;
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
        if (xferRate != kParityRate)
            (*slep)[sfTransferRate] = xferRate.value;
    }

    // Deduct/lock the escrowed amount from the sender.
    AccountID const issuer = amount.getIssuer();
    if (isXRP(amount))
    {
        (*sle)[sfBalance] = (*sle)[sfBalance] - amount;
        sle.update();
    }
    else
    {
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowLockApplyHelper<T>(ctx_.view(), issuer, accountID_, amount, j_);
                },
                amount.asset().value());
            !isTesSuccess(ret))
        {
            return ret;  // LCOV_EXCL_LINE
        }
    }

    // Link the escrow into the sender's owner directory (counts toward reserve),
    // plus the destination and (for IOU) issuer tracking directories where
    // applicable, bump the sender's OwnerCount, stamp any reserve sponsor, and
    // insert. See EscrowEntry::ownerDirs() and SLEBase::create().
    return slep.create(preFeeBalance_);
}

void
EscrowCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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
