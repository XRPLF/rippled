#include <xrpl/tx/transactors/dex/AMMCreate.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/OrderBookDB.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AMMCurve.h>
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
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
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

    if (ctx.tx[sfTradingFee] > kTradingFeeThreshold)
    {
        JLOG(ctx.j.debug()) << "AMM Instance: invalid trading fee.";
        return temBAD_FEE;
    }

    if (ctx.tx.isFieldPresent(sfCurveType))
    {
        if (!ctx.rules.enabled(featureAMMCurves))
        {
            JLOG(ctx.j.debug()) << "AMM Instance: AMMCurves amendment not enabled.";
            return temDISABLED;
        }

        auto const curveType = ctx.tx.getFieldU8(sfCurveType);
        // CurveType 4 (Smart AMM) is reserved for a separate, yet-to-be
        // declared amendment. Return temDISABLED so clients can present
        // a "try again when activated" message.
        if (curveType == 4u)
        {
            JLOG(ctx.j.debug()) << "AMM Instance: Smart AMM amendment not enabled.";
            return temDISABLED;
        }
        if (curveType > CtBinned)
        {
            JLOG(ctx.j.debug()) << "AMM Instance: invalid curve type.";
            return temMALFORMED;
        }

        if (curveType == CtBinned)
        {
            if (!ctx.rules.enabled(featureAMMCurves))
            {
                JLOG(ctx.j.debug()) << "AMM Instance: AMMCurves not enabled.";
                return temDISABLED;
            }
            if (!ctx.tx.isFieldPresent(sfBinStep))
            {
                JLOG(ctx.j.debug()) << "AMM Instance: BinStep required for CtBinned.";
                return temMALFORMED;
            }
            auto const binStep = ctx.tx.getFieldU16(sfBinStep);
            bool valid = false;
            for (std::uint8_t i = 0; i < binStepCount; ++i)
            {
                if (validBinSteps[i] == binStep)
                {
                    valid = true;
                    break;
                }
            }
            if (!valid)
            {
                JLOG(ctx.j.debug()) << "AMM Instance: invalid BinStep value.";
                return temMALFORMED;
            }
        }
        else if (curveType != CtConstantProduct)
        {
            auto const* curve = getCurve(curveType, ctx.rules);
            if (curve == nullptr)
            {
                JLOG(ctx.j.debug()) << "AMM Instance: curve not available.";
                return temDISABLED;
            }

            // All current CurveInterface::validateParams implementations
            // return either tesSUCCESS or temMALFORMED, so masking to
            // temMALFORMED here is lossless.
            if (auto const ter = curve->validateParams(ctx.tx); ter != tesSUCCESS)
            {
                JLOG(ctx.j.debug()) << "AMM Instance: invalid curve params.";
                return temMALFORMED;
            }
        }
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

    auto const curveType = ctx.tx.isFieldPresent(sfCurveType) ? ctx.tx.getFieldU8(sfCurveType)
                                                              : std::uint8_t(CtConstantProduct);

    // Check if AMM already exists for the token pair and curve type
    if (auto const ammKeylet = keylet::amm(amount.asset(), amount2.asset(), curveType);
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
    if (auto const ter = checkFrozen(ctx.view, accountID, amount.asset()); !isTesSuccess(ter))

    {
        JLOG(ctx.j.debug()) << "AMM Instance: involves frozen or locked asset.";
        return ter;
    }
    if (auto const ter = checkFrozen(ctx.view, accountID, amount2.asset()); !isTesSuccess(ter))
    {
        JLOG(ctx.j.debug()) << "AMM Instance: involves frozen or locked asset.";
        return ter;
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
    if (xrpBalance <= beast::kZero)
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
        if (auto const accountId = pseudoAccountAddress(
                ctx.view, keylet::amm(amount.asset(), amount2.asset(), curveType).key);
            accountId == beast::kZero)
            return terADDRESS_COLLISION;

        auto const isMPTIssuerPseudo = [&](Asset const& asset) {
            if (asset.native())
                return false;

            if (asset.holds<Issue>())
                return false;

            return isPseudoAccount(ctx.view, asset.getIssuer());
        };

        if (isMPTIssuerPseudo(amount.asset()) || isMPTIssuerPseudo(amount2.asset()))
        {
            JLOG(ctx.j.debug()) << "AMM Instance: can't create with vault shares " << amount << " "
                                << amount2;
            return tecWRONG_ASSET;
        }
    }

    if (auto const ter = canMPTTradeAndTransfer(ctx.view, amount.asset(), accountID, accountID);
        !isTesSuccess(ter))
        return ter;
    if (auto const ter = canMPTTradeAndTransfer(ctx.view, amount2.asset(), accountID, accountID);
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
                auto const sle = ctx.view.read(keylet::mptokenIssuance(issue.getMptID()));
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
    auto const curveType = ctx.tx.isFieldPresent(sfCurveType) ? ctx.tx.getFieldU8(sfCurveType)
                                                              : std::uint8_t(CtConstantProduct);

    auto const ammKeylet = keylet::amm(amount.asset(), amount2.asset(), curveType);

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
    auto const lptIss = ammLPTIssue(amount.asset(), amount2.asset(), accountId, curveType);
    if (sb.read(keylet::trustLine(accountId, lptIss)))
    {
        JLOG(j.error()) << "AMM Instance: LP Token already exists.";
        return {tecDUPLICATE, false};
    }

    // Note, that the trustlines created by AMM have 0 credit limit.
    // This prevents shifting the balance between accounts via AMM,
    // or sending unsolicited LPTokens. This is a desired behavior.
    // A user can only receive LPTokens through affirmative action -
    // either an AMMDeposit, TrustSet, crossing an offer, etc.

    // Calculate initial LPT balance using curve-specific math.
    //
    // ConcentratedLiquidity is non-fungible: ownership is per-position
    // (ltAMM_POSITION), not per-LP-token. Minting LP tokens at create
    // would strand them — there is no redemption path. So CL pools
    // start with LPTokenBalance = 0 and no LP token transfer. Likewise
    // the Amount / Amount2 in the tx are interpreted as the initial
    // price ratio only; the AMM pool starts with zero asset reserves.
    // First liquidity must come via AMMDeposit, which mints a position
    // SLE spanning a chosen [tickLower, tickUpper] range. This matches
    // the Uniswap v3 / v4 / Trader Joe LB pattern (createPool +
    // separate mint).
    STAmount lpTokens;
    if (curveType == CtConcentratedLiquidity || curveType == CtBinned)
    {
        // Neither curve mints aggregate LP tokens at create — CL uses
        // per-position SLEs, Binned uses per-bin MPT shares. Initial
        // Amount/Amount2 act as the initial price ratio only; no assets
        // are transferred at create time. First liquidity comes via
        // AMMDeposit.
        lpTokens = STAmount{lptIss, 0};
    }
    else if (curveType == CtConstantProduct)
    {
        lpTokens = ammLPTokens(amount, amount2, lptIss);
    }
    else
    {
        auto const* curve = getCurve(curveType, ctx.view().rules());
        auto const& [amt1, amt2] = (amount.asset() < amount2.asset()) ? std::tie(amount, amount2)
                                                                      : std::tie(amount2, amount);
        auto const lpResult = curve->initialLPTokens(amt1, amt2, lptIss, &ctx.tx);
        if (!lpResult)
        {
            JLOG(j.error()) << "AMM Instance: failed to compute initial LP tokens.";
            return {lpResult.error(), false};
        }
        lpTokens = *lpResult;
    }

    // Create ltAMM
    auto ammSle = std::make_shared<SLE>(ammKeylet);
    ammSle->setAccountID(sfAccount, accountId);
    ammSle->setFieldAmount(sfLPTokenBalance, lpTokens);
    auto const& [asset1, asset2] = std::minmax(amount.asset(), amount2.asset());
    ammSle->setFieldIssue(sfAsset, STIssue{sfAsset, asset1});
    ammSle->setFieldIssue(sfAsset2, STIssue{sfAsset2, asset2});

    // Set curve type and params directly on the AMM SLE
    if (curveType != CtConstantProduct)
    {
        ammSle->setFieldU8(sfCurveType, curveType);

        if (curveType == CtConcentratedLiquidity)
        {
            auto const feeTier = ctx.tx.getFieldU8(sfFeeTier);
            auto const tickSpacing = feeTierToTickSpacing[feeTier];

            ammSle->setFieldU8(sfFeeTier, feeTier);
            ammSle->setFieldU16(sfTickSpacing, static_cast<std::uint16_t>(tickSpacing));
            ammSle->setFieldI32(sfCurrentTick, 0);
            ammSle->setFieldU64(sfActiveLiquidity, 0);
            ammSle->setFieldH256(sfSqrtPriceX96, uint256{0});
            ammSle->setFieldNumber(sfFeeGrowthGlobal0, STNumber{sfFeeGrowthGlobal0, Number{0}});
            ammSle->setFieldNumber(sfFeeGrowthGlobal1, STNumber{sfFeeGrowthGlobal1, Number{0}});
        }
        else if (curveType == CtStableSwap)
        {
            ammSle->setFieldU32(sfAmplification, ctx.tx.getFieldU32(sfAmplification));
        }
        else if (curveType == CtBinned)
        {
            ammSle->setFieldU16(sfBinStep, ctx.tx.getFieldU16(sfBinStep));
            // Active bin starts at 0 (price = 1). LPs deposit into named
            // bin IDs; the AMM tracks which bin holds the current price.
            ammSle->setFieldI32(sfActiveBinID, 0);
        }
    }

    // AMM creator gets the auction slot and the voting slot.
    initializeFeeAuctionVote(ctx.view(), ammSle, account, lptIss, ctx.tx[sfTradingFee]);

    // Add owner directory to link the root account and AMM object.
    if (auto ter = dirLink(sb, accountId, ammSle); ter)
    {
        JLOG(j.debug()) << "AMM Instance: failed to insert owner dir";
        return {ter, false};
    }
    sb.insert(ammSle);

    // Send LPT to LP. Skip for CL and Binned — neither mints aggregate LP
    // tokens (CL uses per-position SLEs; Binned uses per-bin MPT shares).
    TER res = tesSUCCESS;
    if (curveType != CtConcentratedLiquidity && curveType != CtBinned)
    {
        res = accountSend(sb, accountId, account, lpTokens, ctx.journal);
        if (!isTesSuccess(res))
        {
            JLOG(j.debug()) << "AMM Instance: failed to send LPT " << lpTokens;
            return {res, false};
        }
    }

    auto sendAndInitTrustOrMPT = [&](STAmount const& amount) -> TER {
        // Authorize MPT
        return amount.asset().visit(
            [&](MPTIssue const& issue) -> TER {
                auto const& mptIssue = issue;
                auto const& mptID = mptIssue.getMptID();
                // Implicitly authorize MPT asset for AMM pseudo-account.
                std::uint32_t const flags = lsfMPTAMM | lsfMPTAuthorized;
                if (auto const err = requireAuth(sb, mptIssue, accountId, AuthType::WeakAuth);
                    !isTesSuccess(err))
                {
                    return err;
                }

                if (auto const err = createMPToken(sb, mptID, accountId, {}, flags);
                    !isTesSuccess(err))
                    return err;
                // Don't adjust AMM owner count.
                // It's irrelevant for pseudo-account like AMM.
                return accountSend(
                    sb,
                    account,
                    accountId,
                    amount,
                    ctx.journal,
                    {},  // don't sponsor for AMM Trustline
                    WaiveTransferFee::Yes);
            },
            // Set AMM flag on AMM trustline
            [&](Issue const& issue) -> TER {
                if (auto const res = accountSend(
                        sb,
                        account,
                        accountId,
                        amount,
                        ctx.journal,
                        {},  // don't sponsor for AMM Trustline
                        WaiveTransferFee::Yes))
                    return res;
                // Set AMM flag on AMM trustline
                if (!isXRP(amount))
                {
                    SLE::pointer const sleRippleState =
                        sb.peek(keylet::trustLine(accountId, issue));
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

    // For CL and Binned, Amount / Amount2 act as the initial price ratio
    // only — no assets are transferred at create time. First liquidity
    // must come via AMMDeposit, which mints either a position SLE (CL)
    // or a per-bin MPT issuance (Binned); trustlines + the lsfAMMNode
    // flag are established lazily by AMMDeposit's first accountSend.
    if (curveType != CtConcentratedLiquidity && curveType != CtBinned)
    {
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
    }

    JLOG(j.debug()) << "AMM Instance: success " << accountId << " " << ammKeylet.key << " "
                    << lpTokens << " " << amount << " " << amount2;
    auto addOrderBook = [&](Asset const& assetIn, Asset const& assetOut, std::uint64_t uRate) {
        Book const book{assetIn, assetOut, std::nullopt};
        auto const dir = keylet::quality(keylet::book(book), uRate);
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

    auto const result = applyCreate(ctx_, sb, accountID_, j_);
    if (result.second)
        sb.apply(ctx_.rawView());

    return result.first;
}

void
AMMCreate::visitInvariantEntry(bool, SLE::const_ref, SLE::const_ref)
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
