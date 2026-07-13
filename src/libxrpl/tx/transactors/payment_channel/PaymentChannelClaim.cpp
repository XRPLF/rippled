#include <xrpl/tx/transactors/payment_channel/PaymentChannelClaim.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/CredentialHelpers.h>
#include <xrpl/ledger/helpers/EscrowHelpers.h>
#include <xrpl/ledger/helpers/PaymentChannelHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <optional>
#include <variant>

namespace xrpl {

bool
PaymentChannelClaim::checkExtraFeatures(PreflightContext const& ctx)
{
    return !ctx.tx.isFieldPresent(sfCredentialIDs) || ctx.rules.enabled(featureCredentials);
}

std::uint32_t
PaymentChannelClaim::getFlagsMask(PreflightContext const&)
{
    return tfPaymentChannelClaimMask;
}

NotTEC
PaymentChannelClaim::preflight(PreflightContext const& ctx)
{
    if (ctx.rules.enabled(fixCleanup3_2_0) && ctx.tx[sfChannel] == beast::kZero)
        return temMALFORMED;

    auto const bal = ctx.tx[~sfBalance];
    if (bal && *bal <= beast::kZero)
        return temBAD_AMOUNT;

    auto const amt = ctx.tx[~sfAmount];
    if (amt && *amt <= beast::kZero)
        return temBAD_AMOUNT;

    if (((bal && !isXRP(*bal)) || (amt && !isXRP(*amt))) && !ctx.rules.enabled(featureTokenPaychan))
        return temBAD_AMOUNT;

    // Both bal and amt must reference the same asset before comparing,
    // otherwise STAmount comparison throws.
    if (bal && amt && bal->asset() != amt->asset())
        return temBAD_AMOUNT;

    if (bal && amt && *bal > *amt)
        return temBAD_AMOUNT;

    {
        if (ctx.tx.isFlag(tfClose) && ctx.tx.isFlag(tfRenew))
            return temMALFORMED;
    }

    if (auto const sig = ctx.tx[~sfSignature])
    {
        if (!(ctx.tx[~sfPublicKey] && bal))
            return temMALFORMED;

        // Check the signature
        // The signature isn't needed if txAccount == src, but if it's
        // present, check it

        auto const reqBalance = *bal;
        auto const authAmt = amt ? *amt : reqBalance;

        if (reqBalance > authAmt)
            return temBAD_AMOUNT;

        Keylet const k(ltPAYCHAN, ctx.tx[sfChannel]);
        if (!publicKeyType(ctx.tx[sfPublicKey]))
            return temMALFORMED;

        PublicKey const pk(ctx.tx[sfPublicKey]);
        Serializer msg;
        serializePayChanAuthorization(msg, k.key, authAmt);
        if (!verify(pk, msg.slice(), *sig))
            return temBAD_SIGNATURE;
    }

    if (auto const err = credentials::checkFields(ctx.tx, ctx.j); !isTesSuccess(err))
        return err;

    return tesSUCCESS;
}

TER
PaymentChannelClaim::preclaim(PreclaimContext const& ctx)
{
    if (ctx.view.rules().enabled(featureCredentials))
    {
        if (auto const err = credentials::valid(ctx.tx, ctx.view, ctx.tx[sfAccount], ctx.j);
            !isTesSuccess(err))
            return err;
    }

    if (ctx.view.rules().enabled(featureTokenPaychan))
    {
        Keylet const k(ltPAYCHAN, ctx.tx[sfChannel]);
        auto const slep = ctx.view.read(k);
        if (!slep)
            return tecNO_TARGET;

        AccountID const dest = (*slep)[sfDestination];
        STAmount const amount = (*slep)[sfAmount];
        if (!isXRP(amount) && ctx.tx.isFieldPresent(sfBalance))
        {
            if (auto const ret = std::visit(
                    [&]<typename T>(T const&) {
                        return escrowUnlockPreclaimHelper<T>(ctx.view, dest, amount);
                    },
                    amount.asset().value());
                !isTesSuccess(ret))
                return ret;
        }
    }

    return tesSUCCESS;
}

TER
PaymentChannelClaim::doApply()
{
    Keylet const k(ltPAYCHAN, ctx_.tx[sfChannel]);
    auto const slep = ctx_.view().peek(k);
    if (!slep)
        return tecNO_TARGET;

    AccountID const src = (*slep)[sfAccount];
    AccountID const dst = (*slep)[sfDestination];

    auto const curExpiration = (*slep)[~sfExpiration];
    if (isChannelExpired(ctx_.view(), (*slep)[~sfCancelAfter]) ||
        isChannelExpired(ctx_.view(), curExpiration))
    {
        return closeChannel(
            slep,
            ctx_.getApplyViewContext(),
            k.key,
            accountID_,
            ctx_.registry.get().getJournal("View"));
    }

    if (accountID_ != src && accountID_ != dst)
        return tecNO_PERMISSION;

    if (ctx_.tx[~sfBalance])
    {
        auto const chanBalance = slep->getFieldAmount(sfBalance);
        auto const chanFunds = slep->getFieldAmount(sfAmount);
        auto const reqBalance = ctx_.tx[sfBalance];

        // The requested balance must match the channel's asset; otherwise
        // STAmount comparisons/subtractions below would throw.
        if (reqBalance.asset() != chanFunds.asset())
            return temBAD_AMOUNT;
        if (auto const reqAmt = ctx_.tx[~sfAmount]; reqAmt && reqAmt->asset() != chanFunds.asset())
            return temBAD_AMOUNT;

        if (accountID_ == dst && !ctx_.tx[~sfSignature])
        {
            return ctx_.view().rules().enabled(fixCleanup3_2_0) ? TER{tecNO_PERMISSION}
                                                                : TER{temBAD_SIGNATURE};
        }

        if (ctx_.tx[~sfSignature])
        {
            PublicKey const pk((*slep)[sfPublicKey]);
            if (ctx_.tx[sfPublicKey] != pk)
            {
                return ctx_.view().rules().enabled(fixCleanup3_2_0) ? TER{tecNO_PERMISSION}
                                                                    : TER{temBAD_SIGNER};
            }
        }

        if (reqBalance > chanFunds)
            return tecUNFUNDED_PAYMENT;

        if (reqBalance <= chanBalance)
        {
            // nothing requested
            return tecUNFUNDED_PAYMENT;
        }

        auto const sled = ctx_.view().peek(keylet::account(dst));
        if (!sled)
            return tecNO_DST;

        if (auto err =
                verifyDepositPreauth(ctx_.tx, ctx_.view(), accountID_, dst, sled, ctx_.journal);
            !isTesSuccess(err))
            return err;

        (*slep)[sfBalance] = ctx_.tx[sfBalance];
        STAmount const reqDelta = reqBalance - chanBalance;
        XRPL_ASSERT(
            reqDelta >= beast::kZero, "xrpl::PaymentChannelClaim::doApply : minimum balance delta");

        // Transfer amount to destination
        if (isXRP(reqDelta))
        {
            (*sled)[sfBalance] = (*sled)[sfBalance] + reqDelta;
        }
        else
        {
            if (!ctx_.view().rules().enabled(featureTokenPaychan))
                return temDISABLED;

            Rate lockedRate = slep->isFieldPresent(sfTransferRate)
                ? xrpl::Rate(slep->getFieldU32(sfTransferRate))
                : kParityRate;
            auto const issuer = reqDelta.getIssuer();
            bool const createAsset = dst == accountID_;
            if (auto const ret = std::visit(
                    [&]<typename T>(T const&) {
                        return escrowUnlockApplyHelper<T>(
                            ctx_.getApplyViewContext(),
                            lockedRate,
                            sled,
                            preFeeBalance_,
                            reqDelta,
                            issuer,
                            src,
                            dst,
                            createAsset,
                            j_);
                    },
                    reqDelta.asset().value());
                !isTesSuccess(ret))
                return ret;
        }

        ctx_.view().update(sled);
        ctx_.view().update(slep);
    }

    if (ctx_.tx.isFlag(tfRenew))
    {
        if (src != accountID_)
            return tecNO_PERMISSION;
        (*slep)[~sfExpiration] = std::nullopt;
        ctx_.view().update(slep);
    }

    if (ctx_.tx.isFlag(tfClose))
    {
        // Channel will close immediately if dry or the receiver closes
        if (dst == accountID_ || (*slep)[sfBalance] == (*slep)[sfAmount])
        {
            return closeChannel(
                slep,
                ctx_.getApplyViewContext(),
                k.key,
                accountID_,
                ctx_.registry.get().getJournal("View"));
        }

        auto const settleExpiration = saturatingAdd(
            ctx_.view().rules(),
            ctx_.view().header().parentCloseTime.time_since_epoch().count(),
            (*slep)[sfSettleDelay]);

        if (!curExpiration || *curExpiration > settleExpiration)
        {
            (*slep)[~sfExpiration] = settleExpiration;
            ctx_.view().update(slep);
        }
    }

    return tesSUCCESS;
}

void
PaymentChannelClaim::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
{
    // No transaction-specific invariants yet (future work).
}

bool
PaymentChannelClaim::finalizeInvariants(
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
