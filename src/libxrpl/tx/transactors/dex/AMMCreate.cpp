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
    return calculateOwnerReserveFee(view, tx);
}

TER
AMMCreate::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];
    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

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
            return (issuerAccount->getFlags() & lsfDefaultRipple) == 0;

        return false;
    };

    if (noDefaultRipple(ctx.view, amount.asset()) || noDefaultRipple(ctx.view, amount2.asset()))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: DefaultRipple not set";
        return terNO_RIPPLE;
    }

    // xrpLiquid accounts for one extra reserve (the LP-token trustline the
    // creator will receive), so a non-positive result means the account cannot
    // afford both the trustline and its current reserve obligations.
    STAmount const xrpBalance = xrpLiquid(ctx.view, accountID, 1, ctx.j);
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

    // featureAMMClawback provides a controlled clawback path for AMM pools.
    // Until it is active, block creation with any clawback-enabled issuer to
    // prevent an issuer from unilaterally draining the pool.
    if (ctx.view.rules().enabled(featureAMMClawback))
        return tesSUCCESS;

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

/** Execute all ledger mutations required to bootstrap a new AMM pool.
 *
 *  This function performs every write inside the supplied @p sb sandbox so the
 *  entire operation is atomic: any error causes the caller to discard the
 *  sandbox, leaving the ledger unchanged. On success the caller flushes @p sb
 *  to the live view.
 *
 *  Mutation sequence:
 *  1. Create a pseudo-account `AccountRoot` derived from the AMM keylet hash
 *     via `createPseudoAccount`. The account carries `sfAMMID`, disables the
 *     master key, and has no regular key — it is exclusively protocol-managed.
 *  2. Compute the initial LP token supply as `sqrt(amount * amount2)` (the
 *     geometric mean, per the constant-product seeding formula) and send the
 *     tokens from the pseudo-account to @p account. LP-token trustlines are
 *     intentionally created with a zero credit limit so the pool cannot push
 *     tokens to an account that has not taken affirmative action (deposit,
 *     `TrustSet`, or offer crossing).
 *  3. Build and insert the `ltAMM` SLE. Assets are stored in canonical
 *     `std::minmax` order regardless of the transaction's input order.
 *     `initializeFeeAuctionVote` populates the trading fee, initial auction
 *     slot, and voting weight — the creator receives the first auction slot.
 *  4. Transfer each seed asset from @p account to the pseudo-account via
 *     `sendAndInitTrustOrMPT` with `WaiveTransferFee::Yes`. For IOU assets
 *     the resulting trust line is immediately stamped with `lsfAMMNode` to
 *     distinguish it from ordinary LP trust lines. For MPT assets,
 *     `createMPToken` establishes the pseudo-account's MPT holding with
 *     `lsfMPTAMM`; if the issuance requires authorization and the
 *     pseudo-account has not been pre-authorized, `lsfMPTAuthorized` is also
 *     set. Owner count is not adjusted for the pseudo-account — it has no
 *     reserve obligation.
 *  5. Register both swap directions (asset1→asset2 and asset2→asset1) with
 *     `OrderBookDB`, making the new pool immediately visible to the payment
 *     engine and offer-crossing logic.
 *
 *  @param ctx      The apply context providing the transaction, raw view, and
 *      service registry.
 *  @param sb       The sandbox overlay; caller commits it on success.
 *  @param account  The creator account that seeds liquidity and receives LP
 *      tokens.
 *  @param j        Journal for diagnostic logging.
 *  @return A pair of `{TER, bool}` where the bool is true only when the TER
 *      is `tesSUCCESS` and the sandbox should be committed.
 */
static std::pair<TER, bool>
applyCreate(ApplyContext& ctx, Sandbox& sb, AccountID const& account, beast::Journal j)
{
    auto const amount = ctx.tx[sfAmount];
    auto const amount2 = ctx.tx[sfAmount2];

    auto const ammKeylet = keylet::amm(amount.asset(), amount2.asset());

    auto const maybeAccount = createPseudoAccount(sb, ammKeylet.key, sfAMMID);
    if (!maybeAccount)
    {
        JLOG(j.error()) << "AMM Instance: failed to create pseudo account.";
        return {maybeAccount.error(), false};
    }
    auto& acc = *maybeAccount;
    auto const accountId = (*acc)[sfAccount];

    auto const lptIss = ammLPTIssue(amount.asset(), amount2.asset(), accountId);
    if (sb.read(keylet::line(accountId, lptIss)))
    {
        JLOG(j.error()) << "AMM Instance: LP Token already exists.";
        return {tecDUPLICATE, false};
    }

    auto const lpTokens = ammLPTokens(amount, amount2, lptIss);

    auto ammSle = std::make_shared<SLE>(ammKeylet);
    ammSle->setAccountID(sfAccount, accountId);
    ammSle->setFieldAmount(sfLPTokenBalance, lpTokens);
    auto const& [asset1, asset2] = std::minmax(amount.asset(), amount2.asset());
    ammSle->setFieldIssue(sfAsset, STIssue{sfAsset, asset1});
    ammSle->setFieldIssue(sfAsset2, STIssue{sfAsset2, asset2});
    initializeFeeAuctionVote(ctx.view(), ammSle, account, lptIss, ctx.tx[sfTradingFee]);

    if (auto ter = dirLink(sb, accountId, ammSle); ter)
    {
        JLOG(j.debug()) << "AMM Instance: failed to insert owner dir";
        return {ter, false};
    }
    sb.insert(ammSle);

    auto res = accountSend(sb, accountId, account, lpTokens, ctx.journal);
    if (!isTesSuccess(res))
    {
        JLOG(j.debug()) << "AMM Instance: failed to send LPT " << lpTokens;
        return {res, false};
    }

    auto sendAndInitTrustOrMPT = [&](STAmount const& amount) -> TER {
        return amount.asset().visit(
            [&](MPTIssue const& issue) -> TER {
                auto const& mptIssue = issue;
                auto const& mptID = mptIssue.getMptID();
                std::uint32_t flags = lsfMPTAMM;
                if (auto const err =
                        requireAuth(ctx.view(), mptIssue, accountId, AuthType::WeakAuth);
                    !isTesSuccess(err))
                {
                    if (err == tecNO_AUTH)
                    {
                        // Issuance requires auth and pseudo-account is not
                        // pre-authorized; set lsfMPTAuthorized on creation.
                        flags |= lsfMPTAuthorized;
                    }
                    else
                    {
                        return err;
                    }
                }

                if (auto const err = createMPToken(sb, mptID, accountId, flags); !isTesSuccess(err))
                    return err;
                // Owner count is not adjusted for the pseudo-account; it has
                // no reserve obligation.
                return accountSend(
                    sb, account, accountId, amount, ctx.journal, WaiveTransferFee::Yes);
            },
            [&](Issue const& issue) -> TER {
                if (auto const res = accountSend(
                        sb, account, accountId, amount, ctx.journal, WaiveTransferFee::Yes))
                    return res;
                if (!isXRP(amount))
                {
                    SLE::pointer const sleRippleState = sb.peek(keylet::line(accountId, issue));
                    if (!sleRippleState)
                    {
                        return tecINTERNAL;  // LCOV_EXCL_LINE
                    }

                    auto const flags = sleRippleState->getFlags();
                    // Mark the trust line as AMM-owned so it can be
                    // distinguished from ordinary LP trust lines.
                    sleRippleState->setFieldU32(sfFlags, flags | lsfAMMNode);
                    sb.update(sleRippleState);
                }
                return tesSUCCESS;
            });
    };

    res = sendAndInitTrustOrMPT(amount);
    if (!isTesSuccess(res))
    {
        JLOG(j.debug()) << "AMM Instance: failed to send " << amount;
        return {res, false};
    }

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
