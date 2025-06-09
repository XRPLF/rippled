//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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
#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/misc/AMMHelpers.h>
#include <xrpld/app/misc/AMMUtils.h>
#include <xrpld/app/misc/MPTUtils.h>
#include <xrpld/app/tx/detail/AMMCreate.h>
#include <xrpld/app/tx/detail/MPTokenAuthorize.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
AMMCreate::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    if (!ctx.rules.enabled(featureMPTokensV2) &&
        (amount.holds<MPTIssue>() || amount2.holds<MPTIssue>()))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid flags.";
        return temINVALID_FLAG;
    }

    if (amount.asset() == amount2.asset())
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: tokens can not have the same asset.";
        return temBAD_AMM_TOKENS;
    }

    if (auto const err = invalidAMMAmount(amount))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid asset1 amount.";
        return err;
    }

    if (auto const err = invalidAMMAmount(amount2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid asset2 amount.";
        return err;
    }

    if (ctx.tx[sfTradingFee] > TRADING_FEE_THRESHOLD)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid trading fee.";
        return temBAD_FEE;
    }

    return preflight2(ctx);
}

XRPAmount
AMMCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AMMCreate is one owner reserve.
    return view.fees().increment;
}

TER
AMMCreate::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    // Check if AMM already exists for the token pair
    if (auto const ammKeylet = keylet::amm(amount.asset(), amount2.asset());
        ctx.view.read(ammKeylet))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: ltAMM already exists.";
        return tecDUPLICATE;
    }

    if (auto const ter = requireAuth(ctx.view, amount.asset(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: account is not authorized, " << amount.asset();
        return ter;
    }

    if (auto const ter = requireAuth(ctx.view, amount2.asset(), accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: account is not authorized, " << amount2.asset();
        return ter;
    }

    // Globally or individually frozen
    if (isFrozen(ctx.view, accountID, amount.asset()) ||
        isFrozen(ctx.view, accountID, amount2.asset()))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: involves frozen asset.";
        return tecFROZEN;
    }

    auto noDefaultRipple = [](ReadView const& view, Asset const& asset) {
        if (asset.holds<MPTIssue>() || isXRP(asset))
            return false;

        if (auto const issuerAccount =
                view.read(keylet::account(asset.getIssuer())))
            return (issuerAccount->getFlags() & lsfDefaultRipple) == 0;

        return false;
    };

    if (noDefaultRipple(ctx.view, amount.asset()) ||
        noDefaultRipple(ctx.view, amount2.asset()))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: DefaultRipple not set";
        return terNO_RIPPLE;
    }

    // Check the reserve for LPToken trustline
    STAmount const xrpBalance = xrpLiquid(ctx.view, accountID, 1, ctx.j);
    // Insufficient reserve
    if (xrpBalance <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: insufficient reserves";
        return tecINSUF_RESERVE_LINE;
    }

    auto insufficientBalance = [&](STAmount const& amount) {
        if (isXRP(amount))
            return xrpBalance < amount;
        return accountID != amount.asset().getIssuer() &&
            accountHolds(
                ctx.view,
                accountID,
                amount.asset(),
                FreezeHandling::fhZERO_IF_FROZEN,
                AuthHandling::ahZERO_IF_UNAUTHORIZED,
                ctx.j) < amount;
    };

    if (insufficientBalance(amount) || insufficientBalance(amount2))
    {
        JLOG(ctx.j.debug())
            << "AMM Instance: insufficient funds, " << amount << " " << amount2;
        return tecUNFUNDED_AMM;
    }

    auto isLPToken = [&](STAmount const& amount) -> bool {
        if (auto const sle =
                ctx.view.read(keylet::account(amount.asset().getIssuer())))
            return sle->isFieldPresent(sfAMMID);
        return false;
    };

    if (isLPToken(amount) || isLPToken(amount2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: can't create with LPTokens "
                            << amount << " " << amount2;
        return tecAMM_INVALID_TOKENS;
    }

    if (ctx.view.rules().enabled(featureSingleAssetVault))
    {
        if (auto const accountId = pseudoAccountAddress(
                ctx.view, keylet::amm(amount.asset(), amount2.asset()).key);
            accountId == beast::zero)
            return terADDRESS_COLLISION;
    }

    if (auto const ter =
            isMPTTxAllowed(ctx.view, ttAMM_CREATE, amount.asset(), accountID);
        ter != tesSUCCESS)
        return ter;
    if (auto const ter =
            isMPTTxAllowed(ctx.view, ttAMM_CREATE, amount2.asset(), accountID);
        ter != tesSUCCESS)
        return ter;

    // If featureAMMClawback is enabled, allow AMMCreate without checking
    // if the issuer has clawback enabled
    if (ctx.view.rules().enabled(featureAMMClawback))
        return tesSUCCESS;

    // Disallow AMM if the issuer has clawback enabled when featureAMMClawback
    // is not enabled
    auto clawbackDisabled = [&](Asset const& asset) -> TER {
        if (isXRP(asset))
            return tesSUCCESS;
        if (asset.holds<MPTIssue>())
        {
            if (auto const sle = ctx.view.read(
                    keylet::mptIssuance(asset.get<MPTIssue>().getMptID()));
                !sle)
                return tecINTERNAL;
            else if (sle->getFlags() & lsfMPTCanClawback)
                return tecNO_PERMISSION;
        }
        else
        {
            if (auto const sle =
                    ctx.view.read(keylet::account(asset.getIssuer()));
                !sle)
                return tecINTERNAL;
            else if (sle->getFlags() & lsfAllowTrustLineClawback)
                return tecNO_PERMISSION;
        }
        return tesSUCCESS;
    };

    if (auto const ter = clawbackDisabled(amount.asset()); ter != tesSUCCESS)
        return ter;
    if (auto const ter = clawbackDisabled(amount2.asset()); ter != tesSUCCESS)
        return ter;

    return tesSUCCESS;
}

static std::pair<TER, bool>
applyCreate(
    ApplyContext& ctx_,
    Sandbox& sb,
    AccountID const& account_,
    beast::Journal j_)
{
    auto const amount = ctx_.tx[sfAmount];
    auto const amount2 = ctx_.tx[sfAmount2];

    auto const ammKeylet = keylet::amm(amount.asset(), amount2.asset());

    // Mitigate same account exists possibility
    auto const maybeAccount = createPseudoAccount(sb, ammKeylet.key, sfAMMID);
    // AMM account already exists (should not happen)
    if (!maybeAccount)
    {
        JLOG(j_.error()) << "AMM Instance: failed to create pseudo account.";
        return {maybeAccount.error(), false};
    }
    auto& account = *maybeAccount;
    auto const accountId = (*account)[sfAccount];

    // LP Token already exists. (should not happen)
    auto const lptIss = ammLPTIssue(amount.asset(), amount2.asset(), accountId);
    if (sb.read(keylet::line(accountId, lptIss)))
    {
        JLOG(j_.error()) << "AMM Instance: LP Token already exists.";
        return {tecDUPLICATE, false};
    }

    // Note, that the trustlines created by AMM have 0 credit limit.
    // This prevents shifting the balance between accounts via AMM,
    // or sending unsolicited LPTokens. This is a desired behavior.
    // A user can only receive LPTokens through affirmative action -
    // either an AMMDeposit, TrustSet, crossing an offer, etc.

    // Calculate initial LPT balance.
    auto const lpTokens = ammLPTokens(amount, amount2, lptIss);

    // Create ltAMM
    auto ammSle = std::make_shared<SLE>(ammKeylet);
    ammSle->setAccountID(sfAccount, accountId);
    ammSle->setFieldAmount(sfLPTokenBalance, lpTokens);
    auto const& [asset1, asset2] = std::minmax(amount.asset(), amount2.asset());
    ammSle->setFieldIssue(sfAsset, STIssue{sfAsset, asset1});
    ammSle->setFieldIssue(sfAsset2, STIssue{sfAsset2, asset2});
    // AMM creator gets the auction slot and the voting slot.
    initializeFeeAuctionVote(
        ctx_.view(), ammSle, account_, lptIss, ctx_.tx[sfTradingFee]);

    // Add owner directory to link the root account and AMM object.
    if (auto ter = dirLink(sb, accountId, ammSle); ter)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to insert owner dir";
        return {ter, false};
    }
    sb.insert(ammSle);

    // Send LPT to LP.
    auto res = accountSend(sb, accountId, account_, lpTokens, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send LPT " << lpTokens;
        return {res, false};
    }

    auto sendAndInitTrustOrMPT = [&](STAmount const& amount) -> TER {
        // Authorize MPT
        if (amount.holds<MPTIssue>())
        {
            auto const& mptIssue = amount.get<MPTIssue>();
            auto const& mptID = mptIssue.getMptID();
            std::uint32_t flags = lsfMPTAMM;
            if (auto const err = requireAuth(
                    ctx_.view(), mptIssue, accountId, MPTAuthType::WeakAuth);
                err != tesSUCCESS)
            {
                if (err == tecNO_AUTH)
                    flags |= lsfMPTAuthorized;
                else
                    return err;
            }

            if (auto const err = MPTokenAuthorize::createMPToken(
                    sb, mptID, accountId, flags);
                err != tesSUCCESS)
                return err;
            // Don't adjust AMM owner count.
            // It's irrelevant for pseudo-account like AMM.
        }

        if (auto const res = accountSend(
                sb,
                account_,
                accountId,
                amount,
                ctx_.journal,
                WaiveTransferFee::Yes))
            return res;

        // Set AMM flag on AMM trustline
        if (amount.holds<Issue>() && !isXRP(amount))
        {
            if (SLE::pointer sleRippleState =
                    sb.peek(keylet::line(accountId, amount.get<Issue>()));
                !sleRippleState)
                return tecINTERNAL;
            else
            {
                auto const flags = sleRippleState->getFlags();
                sleRippleState->setFieldU32(sfFlags, flags | lsfAMMNode);
                sb.update(sleRippleState);
            }
        }

        return tesSUCCESS;
    };

    // Send asset1.
    res = sendAndInitTrustOrMPT(amount);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send " << amount;
        return {res, false};
    }

    // Send asset2.
    res = sendAndInitTrustOrMPT(amount2);
    if (res != tesSUCCESS)
    {
        JLOG(j_.debug()) << "AMM Instance: failed to send " << amount2;
        return {res, false};
    }

    JLOG(j_.debug()) << "AMM Instance: success " << accountId << " "
                     << ammKeylet.key << " " << lpTokens << " " << amount << " "
                     << amount2;
    auto addOrderBook =
        [&](Asset const& assetIn, Asset const& assetOut, std::uint64_t uRate) {
            Book const book{assetIn, assetOut, std::nullopt};
            auto const dir = keylet::quality(keylet::book(book), uRate);
            if (auto const bookExisted = static_cast<bool>(sb.read(dir));
                !bookExisted)
                ctx_.app.getOrderBookDB().addOrderBook(book);
        };
    addOrderBook(amount.asset(), amount2.asset(), getRate(amount2, amount));
    addOrderBook(amount2.asset(), amount.asset(), getRate(amount, amount2));

    return {res, res == tesSUCCESS};
}

TER
AMMCreate::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    auto const result = applyCreate(ctx_, sb, account_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple
