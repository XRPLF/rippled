#include <xrpld/app/misc/HashRouter.h>
#include <xrpld/app/tx/detail/Escrow.h>
#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/app/wasm/HostFuncImpl.h>
#include <xrpld/app/wasm/WasmVM.h>
#include <xrpld/conditions/Condition.h>
#include <xrpld/conditions/Fulfillment.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/CredentialHelpers.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

namespace ripple {

// During an EscrowFinish, the transaction must specify both
// a condition and a fulfillment. We track whether that
// fulfillment matches and validates the condition.
constexpr HashRouterFlags SF_CF_INVALID = HashRouterFlags::PRIVATE5;
constexpr HashRouterFlags SF_CF_VALID = HashRouterFlags::PRIVATE6;

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
    if (amount.native() || amount.mpt() > MPTAmount{maxMPTokenAmount} ||
        amount <= beast::zero)
        return temBAD_AMOUNT;

    return tesSUCCESS;
}

XRPAmount
EscrowCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    XRPAmount txnFees{Transactor::calculateBaseFee(view, tx)};
    if (tx.isFieldPresent(sfFinishFunction))
    {
        // 10 base fees for the transaction (1 is in
        // `Transactor::calculateBaseFee`), plus 5 drops per byte
        txnFees += 9 * view.fees().base + 5 * tx[sfFinishFunction].size();
    }
    return txnFees;
}

bool
EscrowCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    if ((ctx.tx.isFieldPresent(sfFinishFunction) ||
         ctx.tx.isFieldPresent(sfData)) &&
        !ctx.rules.enabled(featureSmartEscrow))
        return false;

    return true;
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
                [&]<typename T>(T const&) {
                    return escrowCreatePreflightHelper<T>(ctx);
                },
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

    if (ctx.tx.isFieldPresent(sfFinishFunction) &&
        !ctx.tx.isFieldPresent(sfCancelAfter))
        return temBAD_EXPIRATION;

    // In the absence of a FinishAfter, the escrow can be finished
    // immediately, which can be confusing. When creating an escrow,
    // we want to ensure that either a FinishAfter time is explicitly
    // specified or a completion condition is attached.
    if (!ctx.tx[~sfFinishAfter] && !ctx.tx[~sfCondition] &&
        !ctx.tx[~sfFinishFunction])
    {
        JLOG(ctx.j.debug()) << "Must have at least one of FinishAfter, "
                               "Condition, or FinishFunction.";
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
    }

    if (ctx.tx.isFieldPresent(sfData))
    {
        if (!ctx.tx.isFieldPresent(sfFinishFunction))
        {
            JLOG(ctx.j.debug())
                << "EscrowCreate with Data requires FinishFunction";
            return temMALFORMED;
        }
        auto const data = ctx.tx.getFieldVL(sfData);
        if (data.size() > maxWasmDataLength)
        {
            JLOG(ctx.j.debug()) << "EscrowCreate.Data bad size " << data.size();
            return temMALFORMED;
        }
    }

    if (ctx.tx.isFieldPresent(sfFinishFunction))
    {
        auto const code = ctx.tx.getFieldVL(sfFinishFunction);
        if (code.size() == 0 ||
            code.size() > ctx.app.config().FEES.extension_size_limit)
        {
            JLOG(ctx.j.debug())
                << "EscrowCreate.FinishFunction bad size " << code.size();
            return temMALFORMED;
        }

        HostFunctions mock;
        auto const re =
            preflightEscrowWasm(code, ESCROW_FUNCTION_NAME, {}, &mock, ctx.j);
        if (!isTesSuccess(re))
        {
            JLOG(ctx.j.debug()) << "EscrowCreate.FinishFunction bad WASM";
            return re;
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
    auto const sleRippleState =
        ctx.view.read(keylet::line(account, issuer, amount.getCurrency()));
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
        return tecNO_PERMISSION;  // LCOV_EXCL_LINE

    // If the account does not have the mpt, return tecOBJECT_NOT_FOUND
    if (!ctx.view.exists(keylet::mptoken(issuanceKey.key, account)))
        return tecOBJECT_NOT_FOUND;

    // If the issuer has requireAuth set, check if the account is
    // authorized
    auto const& mptIssue = amount.get<MPTIssue>();
    if (auto const ter =
            requireAuth(ctx.view, mptIssue, account, AuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has requireAuth set, check if the destination is
    // authorized
    if (auto const ter =
            requireAuth(ctx.view, mptIssue, dest, AuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has frozen the account, return tecLOCKED
    if (isFrozen(ctx.view, account, mptIssue))
        return tecLOCKED;

    // If the issuer has frozen the destination, return tecLOCKED
    if (isFrozen(ctx.view, dest, mptIssue))
        return tecLOCKED;

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
    if (issuer == sender)
        return tecINTERNAL;  // LCOV_EXCL_LINE

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
    if (issuer == sender)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    auto const ter = rippleLockEscrowMPT(view, sender, amount, journal);
    if (ter != tesSUCCESS)
        return ter;  // LCOV_EXCL_LINE
    return tesSUCCESS;
}

template <class T>
static uint32_t
calculateAdditionalReserve(T const& finishFunction)
{
    if (!finishFunction)
        return 1;
    // First 500 bytes included in the normal reserve
    // Each additional 500 bytes requires an additional reserve
    return 1 + (finishFunction->size() / 500);
}

TER
EscrowCreate::doApply()
{
    auto const closeTime = ctx_.view().info().parentCloseTime;

    if (ctx_.tx[~sfCancelAfter] && after(closeTime, ctx_.tx[sfCancelAfter]))
        return tecNO_PERMISSION;

    if (ctx_.tx[~sfFinishAfter] && after(closeTime, ctx_.tx[sfFinishAfter]))
        return tecNO_PERMISSION;

    auto const sle = ctx_.view().peek(keylet::account(account_));
    if (!sle)
        return tefINTERNAL;  // LCOV_EXCL_LINE

    // Check reserve and funds availability
    STAmount const amount{ctx_.tx[sfAmount]};
    auto const reserveToAdd =
        calculateAdditionalReserve(ctx_.tx[~sfFinishFunction]);

    auto const reserve =
        ctx_.view().fees().accountReserve((*sle)[sfOwnerCount] + reserveToAdd);

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
            return tecNO_DST;  // LCOV_EXCL_LINE
        if (((*sled)[sfFlags] & lsfRequireDestTag) &&
            !ctx_.tx[~sfDestinationTag])
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
    (*slep)[~sfFinishFunction] = ctx_.tx[~sfFinishFunction];
    (*slep)[~sfData] = ctx_.tx[~sfData];

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
            keylet::ownerDir(account_),
            escrowKeylet,
            describeOwnerDir(account_));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
        (*slep)[sfOwnerNode] = *page;
    }

    // If it's not a self-send, add escrow to recipient's owner directory.
    AccountID const dest = ctx_.tx[sfDestination];
    if (dest != account_)
    {
        auto page = ctx_.view().dirInsert(
            keylet::ownerDir(dest), escrowKeylet, describeOwnerDir(dest));
        if (!page)
            return tecDIR_FULL;  // LCOV_EXCL_LINE
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
            return tecDIR_FULL;  // LCOV_EXCL_LINE
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
        {
            return ret;  // LCOV_EXCL_LINE
        }
    }

    // increment owner count
    adjustOwnerCount(ctx_.view(), sle, reserveToAdd, ctx_.journal);
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

bool
EscrowFinish::checkExtraFeatures(PreflightContext const& ctx)
{
    if (ctx.tx.isFieldPresent(sfCredentialIDs) &&
        !ctx.rules.enabled(featureCredentials))
        return false;

    if (ctx.tx.isFieldPresent(sfComputationAllowance) &&
        !ctx.rules.enabled(featureSmartEscrow))
    {
        return false;
    }
    return true;
}

NotTEC
EscrowFinish::preflight(PreflightContext const& ctx)
{
    auto const cb = ctx.tx[~sfCondition];
    auto const fb = ctx.tx[~sfFulfillment];

    // If you specify a condition, then you must also specify
    // a fulfillment.
    if (static_cast<bool>(cb) != static_cast<bool>(fb))
    {
        JLOG(ctx.j.debug()) << "Condition != Fulfillment";
        return temMALFORMED;
    }

    return tesSUCCESS;
}

NotTEC
EscrowFinish::preflightSigValidated(PreflightContext const& ctx)
{
    auto const cb = ctx.tx[~sfCondition];
    auto const fb = ctx.tx[~sfFulfillment];

    if (cb && fb)
    {
        auto& router = ctx.app.getHashRouter();

        auto const id = ctx.tx.getTransactionID();
        auto const flags = router.getFlags(id);

        // If we haven't checked the condition, check it
        // now. Whether it passes or not isn't important
        // in preflight.
        if (!any(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            if (checkCondition(*fb, *cb))
                router.setFlags(id, SF_CF_VALID);
            else
                router.setFlags(id, SF_CF_INVALID);
        }
    }

    if (auto const allowance = ctx.tx[~sfComputationAllowance]; allowance)
    {
        if (*allowance == 0)
        {
            return temBAD_LIMIT;
        }
        if (*allowance > ctx.app.config().FEES.extension_compute_limit)
        {
            JLOG(ctx.j.debug())
                << "ComputationAllowance too large: " << *allowance;
            return temBAD_LIMIT;
        }
    }

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j);
        !isTesSuccess(err))
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
    if (auto const allowance = tx[~sfComputationAllowance]; allowance)
    {
        // The extra fee is the allowance in drops, rounded up to the nearest
        // whole drop.
        // Integer math rounds down by default, so we add 1 to round up.
        extraFee +=
            ((*allowance) * view.fees().gasPrice) / MICRO_DROPS_PER_DROP + 1;
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
            requireAuth(ctx.view, mptIssue, dest, AuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    // If the issuer has frozen the destination, return tecLOCKED
    if (isFrozen(ctx.view, dest, mptIssue))
        return tecLOCKED;

    return tesSUCCESS;
}

TER
EscrowFinish::preclaim(PreclaimContext const& ctx)
{
    if (ctx.view.rules().enabled(featureCredentials))
    {
        if (auto const err =
                credentials::valid(ctx.tx, ctx.view, ctx.tx[sfAccount], ctx.j);
            !isTesSuccess(err))
            return err;
    }

    if (ctx.view.rules().enabled(featureTokenEscrow) ||
        ctx.view.rules().enabled(featureSmartEscrow))
    {
        // this check is done in doApply before this amendment is enabled
        auto const k = keylet::escrow(ctx.tx[sfOwner], ctx.tx[sfOfferSequence]);
        auto const slep = ctx.view.read(k);
        if (!slep)
            return tecNO_TARGET;

        if (ctx.view.rules().enabled(featureSmartEscrow))
        {
            if (slep->isFieldPresent(sfFinishFunction))
            {
                if (!ctx.tx.isFieldPresent(sfComputationAllowance))
                {
                    JLOG(ctx.j.debug())
                        << "FinishFunction requires ComputationAllowance";
                    return tefWASM_FIELD_NOT_INCLUDED;
                }
            }
            else
            {
                if (ctx.tx.isFieldPresent(sfComputationAllowance))
                {
                    JLOG(ctx.j.debug()) << "FinishFunction not present, "
                                           "ComputationAllowance present";
                    return tefNO_WASM;
                }
            }
        }
        if (ctx.view.rules().enabled(featureTokenEscrow))
        {
            AccountID const dest = (*slep)[sfDestination];
            STAmount const amount = (*slep)[sfAmount];

            if (!isXRP(amount))
            {
                if (auto const ret = std::visit(
                        [&]<typename T>(T const&) {
                            return escrowFinishPreclaimHelper<T>(
                                ctx, dest, amount);
                        },
                        amount.asset().value());
                    !isTesSuccess(ret))
                    return ret;
            }
        }
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
    bool const issuerHigh = issuer > receiver;

    if (senderIssuer)
        return tecINTERNAL;  // LCOV_EXCL_LINE

    if (receiverIssuer)
        return tesSUCCESS;

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
                view,                           // payment sandbox
                recvLow,                        // is dest low?
                issuer,                         // source
                receiver,                       // destination
                trustLineKey.key,               // ledger index
                sleDest,                        // Account to add to
                false,                          // authorize account
                (sleDest->getFlags() & lsfDefaultRipple) == 0,
                false,                          // freeze trust line
                false,                          // deep freeze trust line
                initialBalance,                 // zero initial balance
                Issue(currency, receiver),      // limit of zero
                0,                              // quality in
                0,                              // quality out
                journal);                       // journal
            !isTesSuccess(ter))
        {
            return ter; // LCOV_EXCL_LINE
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
    // payment. In escrow, the fee is deducted from the locked/sending amount,
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

    // validate the line limit if the account submitting txn is not the receiver
    // of the funds
    if (!createAsset)
    {
        auto const sleRippleState = view.peek(trustLineKey);
        if (!sleRippleState)
            return tecINTERNAL;  // LCOV_EXCL_LINE

        // if the issuer is the high, then we use the low limit
        // otherwise we use the high limit
        STAmount const lineLimit = sleRippleState->getFieldAmount(
            issuerHigh ? sfLowLimit : sfHighLimit);

        STAmount lineBalance = sleRippleState->getFieldAmount(sfBalance);

        // flip the sign of the line balance if the issuer is not high
        if (!issuerHigh)
            lineBalance.negate();

        // add the final amount to the line balance
        lineBalance += finalAmt;

        // if the transfer would exceed the line limit return tecLIMIT_EXCEEDED
        if (lineLimit < lineBalance)
            return tecLIMIT_EXCEEDED;
    }

    // if destination is not the issuer then transfer funds
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
            return ter;  // LCOV_EXCL_LINE
        }

        // update owner count.
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
    // payment. In escrow, the fee is deducted from the locked/sending amount,
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
    return rippleUnlockEscrowMPT(
        view,
        sender,
        receiver,
        finalAmt,
        view.rules().enabled(fixTokenEscrowV1) ? amount : finalAmt,
        journal);
}

TER
EscrowFinish::doApply()
{
    auto const k = keylet::escrow(ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);
    auto const slep = ctx_.view().peek(k);
    if (!slep)
    {
        if (ctx_.view().rules().enabled(featureTokenEscrow) ||
            ctx_.view().rules().enabled(featureSmartEscrow))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        return tecNO_TARGET;
    }

    // If a cancel time is present, a finish operation should only succeed prior
    // to that time.
    auto const now = ctx_.view().info().parentCloseTime;

    // Too soon: can't execute before the finish time
    if ((*slep)[~sfFinishAfter] && !after(now, (*slep)[sfFinishAfter]))
        return tecNO_PERMISSION;

    // Too late: can't execute after the cancel time
    if ((*slep)[~sfCancelAfter] && after(now, (*slep)[sfCancelAfter]))
        return tecNO_PERMISSION;

    AccountID const destID = (*slep)[sfDestination];
    auto const sled = ctx_.view().peek(keylet::account(destID));
    if (ctx_.view().rules().enabled(featureSmartEscrow))
    {
        // NOTE: Escrow payments cannot be used to fund accounts.
        if (!sled)
            return tecNO_DST;

        if (auto err = verifyDepositPreauth(
                ctx_.tx, ctx_.view(), account_, destID, sled, ctx_.journal);
            !isTesSuccess(err))
            return err;
    }

    // Check cryptocondition fulfillment
    {
        auto const id = ctx_.tx.getTransactionID();
        auto flags = ctx_.app.getHashRouter().getFlags(id);

        auto const cb = ctx_.tx[~sfCondition];

        // It's unlikely that the results of the check will
        // expire from the hash router, but if it happens,
        // simply re-run the check.
        if (cb && !any(flags & (SF_CF_INVALID | SF_CF_VALID)))
        {
            // LCOV_EXCL_START
            auto const fb = ctx_.tx[~sfFulfillment];

            if (!fb)
                return tecINTERNAL;

            if (checkCondition(*fb, *cb))
                flags = SF_CF_VALID;
            else
                flags = SF_CF_INVALID;

            ctx_.app.getHashRouter().setFlags(id, flags);
            // LCOV_EXCL_STOP
        }

        // If the check failed, then simply return an error
        // and don't look at anything else.
        if (any(flags & SF_CF_INVALID))
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

    if (!ctx_.view().rules().enabled(featureSmartEscrow))
    {
        // NOTE: Escrow payments cannot be used to fund accounts.
        if (!sled)
            return tecNO_DST;

        if (auto err = verifyDepositPreauth(
                ctx_.tx, ctx_.view(), account_, destID, sled, ctx_.journal);
            !isTesSuccess(err))
            return err;
    }

    // Execute custom release function
    if ((*slep)[~sfFinishFunction])
    {
        JLOG(j_.trace())
            << "The escrow has a finish function, running WASM code...";
        // WASM execution
        auto const wasmStr = slep->getFieldVL(sfFinishFunction);
        std::vector<uint8_t> wasm(wasmStr.begin(), wasmStr.end());

        WasmHostFunctionsImpl ledgerDataProvider(ctx_, k);

        if (!ctx_.tx.isFieldPresent(sfComputationAllowance))
        {
            // already checked above, this check is just in case
            return tecINTERNAL;
        }
        std::uint32_t allowance = ctx_.tx[sfComputationAllowance];
        auto re = runEscrowWasm(
            wasm, ESCROW_FUNCTION_NAME, {}, &ledgerDataProvider, allowance);
        JLOG(j_.trace()) << "Escrow WASM ran";

        if (auto const& data = ledgerDataProvider.getData(); data.has_value())
        {
            slep->setFieldVL(sfData, makeSlice(*data));
            ctx_.view().update(slep);
        }

        if (re.has_value())
        {
            auto reValue = re.value().result;
            auto reCost = re.value().cost;
            JLOG(j_.debug()) << "WASM Success: " + std::to_string(reValue)
                             << ", cost: " << reCost;

            ctx_.setWasmReturnCode(reValue);

            if (reCost < 0 || reCost > std::numeric_limits<uint32_t>::max())
                return tecINTERNAL;  // LCOV_EXCL_LINE
            ctx_.setGasUsed(static_cast<uint32_t>(reCost));

            if (reValue <= 0)
            {
                return tecWASM_REJECTED;
            }
        }
        else
        {
            JLOG(j_.debug()) << "WASM Failure: " + transHuman(re.error());
            return re.error();
        }
    }

    AccountID const account = (*slep)[sfAccount];

    // Remove escrow from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(account), page, k.key, true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete Escrow from owner.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    // Remove escrow from recipient's owner directory, if present.
    if (auto const optPage = (*slep)[~sfDestinationNode])
    {
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(destID), *optPage, k.key, true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
        }
    }

    STAmount const amount = slep->getFieldAmount(sfAmount);
    // Transfer amount to destination
    if (isXRP(amount))
        (*sled)[sfBalance] = (*sled)[sfBalance] + amount;
    else
    {
        if (!ctx_.view().rules().enabled(featureTokenEscrow))
            return temDISABLED;  // LCOV_EXCL_LINE

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
                // LCOV_EXCL_START
                JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }
        }
    }

    ctx_.view().update(sled);

    auto const reserveToSubtract =
        calculateAdditionalReserve((*slep)[~sfFinishFunction]);

    // Adjust source owner count
    auto const sle = ctx_.view().peek(keylet::account(account));
    adjustOwnerCount(ctx_.view(), sle, -1 * reserveToSubtract, ctx_.journal);
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);
    return tesSUCCESS;
}

//------------------------------------------------------------------------------

NotTEC
EscrowCancel::preflight(PreflightContext const& ctx)
{
    return tesSUCCESS;
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
    // If the issuer is the same as the account, return tecINTERNAL
    if (issuer == account)
        return tecINTERNAL;  // LCOV_EXCL_LINE

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
    // If the issuer is the same as the account, return tecINTERNAL
    if (issuer == account)
        return tecINTERNAL;  // LCOV_EXCL_LINE

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
            requireAuth(ctx.view, mptIssue, account, AuthType::WeakAuth);
        ter != tesSUCCESS)
        return ter;

    return tesSUCCESS;
}

TER
EscrowCancel::preclaim(PreclaimContext const& ctx)
{
    if (ctx.view.rules().enabled(featureTokenEscrow))
    {
        auto const k = keylet::escrow(ctx.tx[sfOwner], ctx.tx[sfOfferSequence]);
        auto const slep = ctx.view.read(k);
        if (!slep)
            return tecNO_TARGET;

        AccountID const account = (*slep)[sfAccount];
        STAmount const amount = (*slep)[sfAmount];

        if (!isXRP(amount))
        {
            if (auto const ret = std::visit(
                    [&]<typename T>(T const&) {
                        return escrowCancelPreclaimHelper<T>(
                            ctx, account, amount);
                    },
                    amount.asset().value());
                !isTesSuccess(ret))
                return ret;
        }
    }
    return tesSUCCESS;
}

TER
EscrowCancel::doApply()
{
    auto const k = keylet::escrow(ctx_.tx[sfOwner], ctx_.tx[sfOfferSequence]);
    auto const slep = ctx_.view().peek(k);
    if (!slep)
    {
        if (ctx_.view().rules().enabled(featureTokenEscrow))
            return tecINTERNAL;  // LCOV_EXCL_LINE

        return tecNO_TARGET;
    }

    auto const now = ctx_.view().info().parentCloseTime;

    // No cancel time specified: can't execute at all.
    if (!(*slep)[~sfCancelAfter])
        return tecNO_PERMISSION;

    // Too soon: can't execute before the cancel time.
    if (!after(now, (*slep)[sfCancelAfter]))
        return tecNO_PERMISSION;

    AccountID const account = (*slep)[sfAccount];

    // Remove escrow from owner directory
    {
        auto const page = (*slep)[sfOwnerNode];
        if (!ctx_.view().dirRemove(
                keylet::ownerDir(account), page, k.key, true))
        {
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete Escrow from owner.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
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
            // LCOV_EXCL_START
            JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
            return tefBAD_LEDGER;
            // LCOV_EXCL_STOP
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
            return temDISABLED;  // LCOV_EXCL_LINE

        auto const issuer = amount.getIssuer();
        bool const createAsset = account == account_;
        if (auto const ret = std::visit(
                [&]<typename T>(T const&) {
                    return escrowUnlockApplyHelper<T>(
                        ctx_.view(),
                        parityRate,
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
            return ret;  // LCOV_EXCL_LINE

        // Remove escrow from issuers owner directory, if present.
        if (auto const optPage = (*slep)[~sfIssuerNode]; optPage)
        {
            if (!ctx_.view().dirRemove(
                    keylet::ownerDir(issuer), *optPage, k.key, true))
            {
                // LCOV_EXCL_START
                JLOG(j_.fatal()) << "Unable to delete Escrow from recipient.";
                return tefBAD_LEDGER;
                // LCOV_EXCL_STOP
            }
        }
    }

    auto const reserveToSubtract =
        calculateAdditionalReserve((*slep)[~sfFinishFunction]);
    adjustOwnerCount(ctx_.view(), sle, -1 * reserveToSubtract, ctx_.journal);
    ctx_.view().update(sle);

    // Remove escrow from ledger
    ctx_.view().erase(slep);

    return tesSUCCESS;
}

}  // namespace ripple
