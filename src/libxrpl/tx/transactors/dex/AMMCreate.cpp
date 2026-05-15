#include <xrpl/tx/transactors/dex/AMMCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl {

bool
AMMCreate::checkExtraFeatures(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return false;

    if (!ctx.rules.enabled(featureMPTokensV2) &&
        (ctx.tx[sfAmount].holds<MPTIssue>() || ctx.tx[sfAmount2].holds<MPTIssue>()))
        return false;

    return true;
}

NotTEC
AMMCreate::preflight(PreflightContext const& ctx)
{
    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    if (amount.asset() == amount2.asset())
    {
        JLOG(ctx.j.debug()) << "AMM Instance: tokens can not have the same asset.";
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

    if (ctx.tx[sfTradingFee] > kTRADING_FEE_THRESHOLD)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid trading fee.";
        return temBAD_FEE;
    }

    return tesSUCCESS;
}

XRPAmount
AMMCreate::calculateBaseFee(ReadView const& view, STTx const& tx)
{
    // The fee required for AMMCreate is one owner reserve.
    return calculateOwnerReserveFee(view, tx);
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

    if (auto const ter = requireAuth(ctx.view, amount.asset(), accountID); !isTesSuccess(ter))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: account is not authorized, " << amount.asset();
        return ter;
    }

    if (auto const ter = requireAuth(ctx.view, amount2.asset(), accountID); !isTesSuccess(ter))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: account is not authorized, " << amount2.asset();
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

        if (auto const issuerAccount = view.read(keylet::account(asset.getIssuer())))
            return !issuerAccount->isFlag(lsfDefaultRipple);

        return false;
    };

    if (noDefaultRipple(ctx.view, amount.asset()) || noDefaultRipple(ctx.view, amount2.asset()))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: DefaultRipple not set";
        return terNO_RIPPLE;
    }

    // Check the reserve for LPToken trustline
    STAmount const xrpBalance = xrpLiquid(ctx.view, accountID, 1, ctx.j);
    // Insufficient reserve
    if (xrpBalance <= beast::kZERO)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: insufficient reserves";
        return tecINSUF_RESERVE_LINE;
    }

    auto insufficientBalance = [&](STAmount const& amount) {
        if (isXRP(amount))
            return xrpBalance < amount;
        return accountFunds(
                   ctx.view,
                   accountID,
                   amount,
                   FreezeHandling::ZeroIfFrozen,
                   AuthHandling::ZeroIfUnauthorized,
                   ctx.j) < amount;
    };

    if (insufficientBalance(amount) || insufficientBalance(amount2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: insufficient funds, " << amount << " " << amount2;
        return tecUNFUNDED_AMM;
    }

    auto isLPToken = [&](STAmount const& amount) -> bool {
        if (auto const sle = ctx.view.read(keylet::account(amount.asset().getIssuer())))
            return sle->isFieldPresent(sfAMMID);
        return false;
    };

    if (isLPToken(amount) || isLPToken(amount2))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: can't create with LPTokens " << amount << " "
                            << amount2;
        return tecAMM_INVALID_TOKENS;
    }

    if (ctx.view.rules().enabled(featureSingleAssetVault))
    {
        if (auto const accountId =
                pseudoAccountAddress(ctx.view, keylet::amm(amount.asset(), amount2.asset()).key);
            accountId == beast::kZERO)
            return terADDRESS_COLLISION;
    }

    if (auto const ter = checkMPTTxAllowed(ctx.view, ttAMM_CREATE, amount.asset(), accountID);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = checkMPTTxAllowed(ctx.view, ttAMM_CREATE, amount2.asset(), accountID);
        !isTesSuccess(ter))
        return ter;

    // If featureAMMClawback is enabled, allow AMMCreate without checking
    // if the issuer has clawback enabled
    if (ctx.view.rules().enabled(featureAMMClawback))
        return tesSUCCESS;

    // Disallow AMM if the issuer has clawback enabled when featureAMMClawback
    // is not enabled
    auto clawbackDisabled = [&](Asset const& asset) -> TER {
        return asset.visit(
            [&](MPTIssue const& issue) -> TER {
                auto const sle = ctx.view.read(keylet::mptIssuance(issue.getMptID()));
                if (!sle)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                if (sle->isFlag(lsfMPTCanClawback))
                    return tecNO_PERMISSION;
                return tesSUCCESS;
            },
            [&](Issue const& issue) -> TER {
                if (isXRP(issue))
                    return tesSUCCESS;
                auto const sle = ctx.view.read(keylet::account(issue.account));
                if (!sle)
                    return tecINTERNAL;  // LCOV_EXCL_LINE
                if (sle->isFlag(lsfAllowTrustLineClawback))
                    return tecNO_PERMISSION;
                return tesSUCCESS;
            });
    };

    if (auto const ter = clawbackDisabled(amount.asset()); !isTesSuccess(ter))
        return ter;
    if (auto const ter = clawbackDisabled(amount2.asset()); !isTesSuccess(ter))
        return ter;

    return tesSUCCESS;
}

static std::pair<TER, bool>
applyCreate(ApplyContext& ctx, Sandbox& sb, AccountID const& account, beast::Journal j)
{
    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    auto const ammKeylet = keylet::amm(amount.asset(), amount2.asset());

    // Mitigate same account exists possibility
    auto const maybeAccount = createPseudoAccount(sb, ammKeylet.key, sfAMMID);
    // AMM account already exists (should not happen)
    if (!maybeAccount)
    {
        JLOG(j.error()) << "AMM Instance: failed to create pseudo account.";
        return {maybeAccount.error(), false};
    }
    auto& acc = *maybeAccount;
    auto const accountId = (*acc)[sfAccount];

    // LP Token already exists. (should not happen)
    auto const lptIss = ammLPTIssue(amount.asset(), amount2.asset(), accountId);
    if (sb.read(keylet::line(accountId, lptIss)))
    {
        JLOG(j.error()) << "AMM Instance: LP Token already exists.";
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
    initializeFeeAuctionVote(ctx.view(), ammSle, account, lptIss, ctx.tx[sfTradingFee]);

    // Add owner directory to link the root account and AMM object.
    if (auto ter = dirLink(sb, accountId, ammSle); ter)
    {
        JLOG(j.debug()) << "AMM Instance: failed to insert owner dir";
        return {ter, false};
    }
    sb.insert(ammSle);

    // Send LPT to LP.
    auto res = accountSend(sb, accountId, account, lpTokens, ctx.journal);
    if (!isTesSuccess(res))
    {
        JLOG(j.debug()) << "AMM Instance: failed to send LPT " << lpTokens;
        return {res, false};
    }

    auto sendAndInitTrustOrMPT = [&](STAmount const& amount) -> TER {
        // Authorize MPT
        return amount.asset().visit(
            [&](MPTIssue const& issue) -> TER {
                // Authorize MPT
                auto const& mptIssue = issue;
                auto const& mptID = mptIssue.getMptID();
                std::uint32_t flags = lsfMPTAMM;
                if (auto const err =
                        requireAuth(ctx.view(), mptIssue, accountId, AuthType::WeakAuth);
                    !isTesSuccess(err))
                {
                    if (err == tecNO_AUTH)
                    {
                        flags |= lsfMPTAuthorized;
                    }
                    else
                    {
                        return err;
                    }
                }

                if (auto const err = createMPToken(sb, mptID, accountId, flags); !isTesSuccess(err))
                    return err;
                // Don't adjust AMM owner count.
                // It's irrelevant for pseudo-account like AMM.
                return accountSend(
                    sb, account, accountId, amount, ctx.journal, WaiveTransferFee::Yes);
            },
            // Set AMM flag on AMM trustline
            [&](Issue const& issue) -> TER {
                if (auto const res = accountSend(
                        sb, account, accountId, amount, ctx.journal, WaiveTransferFee::Yes))
                    return res;
                // Set AMM flag on AMM trustline
                if (!isXRP(amount))
                {
                    SLE::pointer const sleRippleState = sb.peek(keylet::line(accountId, issue));
                    if (!sleRippleState)
                    {
                        return tecINTERNAL;  // LCOV_EXCL_LINE
                    }

                    auto const flags = sleRippleState->getFlags();
                    sleRippleState->setFieldU32(sfFlags, flags | lsfAMMNode);
                    sb.update(sleRippleState);
                }
                return tesSUCCESS;
            });
    };

    // Send asset1.
    res = sendAndInitTrustOrMPT(amount);
    if (!isTesSuccess(res))
    {
        JLOG(j.debug()) << "AMM Instance: failed to send " << amount;
        return {res, false};
    }

    // Send asset2.
    res = sendAndInitTrustOrMPT(amount2);
    if (!isTesSuccess(res))
    {
        JLOG(j.debug()) << "AMM Instance: failed to send " << amount2;
        return {res, false};
    }

    JLOG(j.debug()) << "AMM Instance: success " << accountId << " " << ammKeylet.key << " "
                    << lpTokens << " " << amount << " " << amount2;
    auto addOrderBook = [&](Asset const& assetIn, Asset const& assetOut, std::uint64_t uRate) {
        Book const book{assetIn, assetOut, std::nullopt};
        auto const dir = keylet::quality(keylet::kBOOK(book), uRate);
        if (auto const bookExisted = static_cast<bool>(sb.read(dir)); !bookExisted)
            ctx.registry.get().getOrderBookDB().addOrderBook(book);
    };
    addOrderBook(amount.asset(), amount2.asset(), getRate(amount2, amount));
    addOrderBook(amount2.asset(), amount.asset(), getRate(amount, amount2));

    return {res, isTesSuccess(res)};
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

void
AMMCreate::visitInvariantEntry(
    bool,
    std::shared_ptr<SLE const> const&,
    std::shared_ptr<SLE const> const&)
{
    // No transaction-specific invariants yet (future work).
}

bool
AMMCreate::finalizeInvariants(STTx const&, TER, XRPAmount, ReadView const&, beast::Journal const&)
{
    // No transaction-specific invariants yet (future work).
    return true;
}

}  // namespace xrpl
