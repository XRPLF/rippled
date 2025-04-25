//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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
#include <xrpld/app/tx/detail/OptionPairCreate.h>
#include <xrpld/ledger/Sandbox.h>
#include <xrpld/ledger/View.h>

#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>

namespace ripple {

NotTEC
OptionPairCreate::preflight(PreflightContext const& ctx)
{
    // Check if the Options feature is enabled
    if (!ctx.rules.enabled(featureOptions))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "OptionPairCreate: invalid flags.";
        return temINVALID_FLAG;
    }

    Issue const issue = ctx.tx[sfAsset].get<Issue>();
    Issue const issue2 = ctx.tx[sfAsset2].get<Issue>();

    // auto const amount = ctx.tx[sfAmount];
    // auto const amount2 = ctx.tx[sfAmount2];

    if (issue == issue2)
    {
        JLOG(ctx.j.error()) << "OptionPairCreate: tokens can not have the same "
                               "currency/issuer.";
        return temMALFORMED;
    }

    if (auto const err = invalidAMMAsset(issue))
    {
        JLOG(ctx.j.debug()) << "OptionPairCreate: invalid asset1.";
        return err;
    }

    if (auto const err = invalidAMMAsset(issue2))
    {
        JLOG(ctx.j.debug()) << "OptionPairCreate: invalid asset2.";
        return err;
    }

    return preflight2(ctx);
}

XRPAmount
OptionPairCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for OptionPairCreate is one owner reserve.
    return view.fees().increment;
}

TER
OptionPairCreate::preclaim(PreclaimContext const& ctx)
{
    return tesSUCCESS;
    auto const accountID = ctx.tx[sfAccount];
    Issue const issue = ctx.tx[sfAsset].get<Issue>();
    Issue const issue2 = ctx.tx[sfAsset2].get<Issue>();

    // Check if OptionPair already exists
    if (auto const optionPairKeylet = keylet::optionPair(issue, issue2);
        ctx.view.read(optionPairKeylet))
    {
        JLOG(ctx.j.debug())
            << "OptionPairCreate: ltOPTION_PAIR already exists.";
        return tecDUPLICATE;
    }

    if (auto const ter = requireAuth(ctx.view, issue, accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "OptionPairCreate: account is not authorized, " << issue;
        return ter;
    }

    if (auto const ter = requireAuth(ctx.view, issue2, accountID);
        ter != tesSUCCESS)
    {
        JLOG(ctx.j.debug())
            << "OptionPairCreate: account is not authorized, " << issue2;
        return ter;
    }

    // Globally or individually frozen
    if (isFrozen(ctx.view, accountID, issue) ||
        isFrozen(ctx.view, accountID, issue2))
    {
        JLOG(ctx.j.debug()) << "OptionPairCreate: involves frozen asset.";
        return tecFROZEN;
    }

    auto noDefaultRipple = [](ReadView const& view, Issue const& issue) {
        if (isXRP(issue))
            return false;

        if (auto const issuerAccount =
                view.read(keylet::account(issue.account)))
            return (issuerAccount->getFlags() & lsfDefaultRipple) == 0;

        return false;
    };

    if (noDefaultRipple(ctx.view, issue) || noDefaultRipple(ctx.view, issue2))
    {
        JLOG(ctx.j.debug()) << "OptionPairCreate: DefaultRipple not set";
        return terNO_RIPPLE;
    }
}

static TER
applyCreate(
    ApplyContext& ctx_,
    Sandbox& sb,
    AccountID const& account_,
    beast::Journal j_)
{
    Issue const issue = ctx_.tx[sfAsset].get<Issue>();
    Issue const issue2 = ctx_.tx[sfAsset2].get<Issue>();

    auto const optionPairKeylet = keylet::optionPair(issue, issue2);

    // Mitigate same account exists possibility
    auto const account = [&]() -> Expected<AccountID, TER> {
        std::uint16_t constexpr maxAccountAttempts = 256;
        for (auto p = 0; p < maxAccountAttempts; ++p)
        {
            auto const account =
                ammAccountID(p, sb.info().parentHash, optionPairKeylet.key);
            if (!sb.read(keylet::account(account)))
                return account;
        }
        return Unexpected(tecDUPLICATE);
    }();

    // account already exists (should not happen)
    if (!account)
    {
        JLOG(j_.error()) << "OptionPairCreate: OptionPair already exists.";
        return account.error();
    }

    // Create OptionPair Root Account.
    auto sleRoot = std::make_shared<SLE>(keylet::account(*account));
    sleRoot->setAccountID(sfAccount, *account);
    sleRoot->setFieldAmount(sfBalance, STAmount{});
    std::uint32_t const seqno{
        ctx_.view().rules().enabled(featureDeletableAccounts)
            ? ctx_.view().seq()
            : 1};
    sleRoot->setFieldU32(sfSequence, seqno);
    sleRoot->setFieldU32(
        sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
    sleRoot->setFieldH256(sfOptionPairID, optionPairKeylet.key);
    sb.insert(sleRoot);

    // Create ltOPTION_PAIR object.
    auto pairSle = std::make_shared<SLE>(optionPairKeylet);
    pairSle->setAccountID(sfAccount, *account);
    auto const& [_issue1, _issue2] = std::minmax(issue, issue2);
    pairSle->setFieldIssue(sfAsset, STIssue{sfAsset, _issue1});
    pairSle->setFieldIssue(sfAsset2, STIssue{sfAsset2, _issue2});

    // Add owner directory to link the root account and AMM object.
    if (auto const page = sb.dirInsert(
            keylet::ownerDir(*account),
            pairSle->key(),
            describeOwnerDir(*account)))
    {
        pairSle->setFieldU64(sfOwnerNode, *page);
    }
    else
    {
        JLOG(j_.debug()) << "OptionPairCreate: failed to insert owner dir";
        return tecDIR_FULL;
    }
    sb.insert(pairSle);

    return tesSUCCESS;
}

TER
OptionPairCreate::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    if (auto const res = applyCreate(ctx_, sb, account_, j_);
        !isTesSuccess(res))
    {
        JLOG(j_.error()) << "OptionPairCreate: failed to create OptionPair.";
        return res;
    }

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace ripple
