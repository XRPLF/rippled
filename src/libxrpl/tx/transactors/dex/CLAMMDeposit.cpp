#include <xrpl/basics/Slice.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/transactors/dex/CLAMMDeposit.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>
#include <xrpl/tx/transactors/nft/NFTokenMint.h>
#include <xrpl/tx/transactors/nft/NFTokenUtils.h>

namespace xrpl {

bool
CLAMMDeposit::checkExtraFeatures(PreflightContext const& ctx)
{
    return clammEnabled(ctx.rules);
}

NotTEC
CLAMMDeposit::preflight(PreflightContext const& ctx)
{
    if (ctx.tx.getFlags() & tfUniversalMask)
        return temINVALID_FLAG;

    // If adding to existing position via NFTokenID, ticks come from position
    if (ctx.tx.isFieldPresent(sfNFTokenID))
    {
        // LowerTick/UpperTick should not be specified (they come from position)
        // But they're soeREQUIRED so they will be present. Validate they
        // match the position in doApply.
    }

    auto const lowerTick = ctx.tx[sfLowerTick];
    auto const upperTick = ctx.tx[sfUpperTick];

    if (lowerTick >= upperTick)
    {
        JLOG(ctx.j.debug()) << "CLAMM Deposit: lower tick must be < upper.";
        return temBAD_AMOUNT;
    }

    if (lowerTick < CLAMM_MIN_TICK || upperTick > CLAMM_MAX_TICK)
    {
        JLOG(ctx.j.debug()) << "CLAMM Deposit: tick out of range.";
        return temBAD_AMOUNT;
    }

    return tesSUCCESS;
}

TER
CLAMMDeposit::preclaim(PreclaimContext const& ctx)
{
    std::shared_ptr<SLE const> sleClamm;
    if (auto const poolID = resolvePoolID(ctx.tx))
    {
        sleClamm = ctx.view.read(keylet::clamm(*poolID));
        if (!sleClamm)
        {
            JLOG(ctx.j.debug()) << "CLAMM Deposit: pool not found.";
            return tecNO_ENTRY;
        }
    }

    // Check if pool assets are frozen for the depositor
    if (sleClamm)
    {
        auto const accountID = ctx.tx[sfAccount];
        auto const& clammRef = std::as_const(*sleClamm);
        auto const issue0 = clammRef[sfAsset].get<Issue>();
        auto const issue1 = clammRef[sfAsset2].get<Issue>();
        auto const ammAccountID = sleClamm->getAccountID(sfAccount);

        for (auto const& issue : {issue0, issue1})
        {
            if (isXRP(issue))
                continue;

            // Check if AMM account or currency is frozen
            if (isFrozen(ctx.view, ammAccountID, issue))
            {
                JLOG(ctx.j.debug())
                    << "CLAMM Deposit: pool asset is frozen, " << issue;
                return tecFROZEN;
            }

            // Check if depositor's trust line is individually frozen
            if (isIndividualFrozen(
                    ctx.view, accountID, issue.currency, issue.account))
            {
                JLOG(ctx.j.debug())
                    << "CLAMM Deposit: depositor asset frozen, " << issue;
                return tecFROZEN;
            }
        }
    }

    // If adding to existing position, verify it exists
    if (ctx.tx.isFieldPresent(sfNFTokenID))
    {
        auto const nfTokenID = ctx.tx.getFieldH256(sfNFTokenID);
        if (!ctx.view.read(keylet::clammPosition(nfTokenID)))
        {
            JLOG(ctx.j.debug())
                << "CLAMM Deposit: existing position not found.";
            return tecNO_ENTRY;
        }
    }

    return tesSUCCESS;
}

TER
CLAMMDeposit::doApply()
{
    auto const account = ctx_.tx[sfAccount];
    auto const optPoolID = resolvePoolID(ctx_.tx);
    if (!optPoolID)
    {
        JLOG(j_.debug()) << "CLAMM Deposit: no pool identifier provided.";
        return temMALFORMED;
    }
    auto const poolID = *optPoolID;
    auto const lowerTick = ctx_.tx[sfLowerTick];
    auto const upperTick = ctx_.tx[sfUpperTick];

    Sandbox sb(&ctx_.view());

    // Read pool SLE
    auto const clammKeylet = keylet::clamm(poolID);
    auto sleClamm = sb.peek(clammKeylet);
    if (!sleClamm)
    {
        JLOG(j_.debug()) << "CLAMM Deposit: pool not found in doApply.";
        return tecNO_ENTRY;
    }

    auto const ammAccountID = sleClamm->getAccountID(sfAccount);
    auto const tickSpacing = sleClamm->getFieldU16(sfTickSpacing);

    // Validate tick alignment
    if (!isValidCLAMMTick(lowerTick, tickSpacing) ||
        !isValidCLAMMTick(upperTick, tickSpacing))
    {
        JLOG(j_.debug()) << "CLAMM Deposit: ticks not aligned to spacing.";
        return temBAD_AMOUNT;
    }

    // Check if adding to existing position
    bool const addToExisting = ctx_.tx.isFieldPresent(sfNFTokenID);
    std::shared_ptr<SLE> sleExistingPos;
    if (addToExisting)
    {
        auto const nfTokenID = ctx_.tx.getFieldH256(sfNFTokenID);
        sleExistingPos = sb.peek(keylet::clammPosition(nfTokenID));
        if (!sleExistingPos)
            return tecNO_ENTRY;

        // Verify caller owns the position
        if (sleExistingPos->getAccountID(sfOwner) != account)
        {
            JLOG(j_.debug())
                << "CLAMM Deposit: caller does not own position.";
            return tecNO_PERMISSION;
        }

        // Verify position belongs to this pool
        if (sleExistingPos->getFieldH256(sfPoolID) != poolID)
        {
            JLOG(j_.debug())
                << "CLAMM Deposit: position pool mismatch.";
            return tecNO_ENTRY;
        }

        // Verify ticks match
        if (sleExistingPos->getFieldI32(sfLowerTick) != lowerTick ||
            sleExistingPos->getFieldI32(sfUpperTick) != upperTick)
        {
            JLOG(j_.debug())
                << "CLAMM Deposit: tick range mismatch with existing position.";
            return tecNO_PERMISSION;
        }
    }

    // Read current pool state
    auto const sqrtPriceCurrent =
        clamm::fromSLEField(sleClamm->getFieldH128(sfSqrtPrice));
    auto const currentTick = sleClamm->getFieldI32(sfCurrentTick);
    auto const feeGrowthGlobal0 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal0));
    auto const feeGrowthGlobal1 =
        clamm::fromSLEField(sleClamm->getFieldH128(sfFeeGrowthGlobal1));

    // Compute sqrt prices at tick boundaries
    auto const sqrtPriceLower = clamm::tickToSqrtPrice(lowerTick);
    auto const sqrtPriceUpper = clamm::tickToSqrtPrice(upperTick);

    // Determine liquidity amount
    clamm::uint128 liquidity;

    if (ctx_.tx.isFieldPresent(sfLiquidityAmount))
    {
        liquidity =
            clamm::fromSLEField(ctx_.tx.getFieldH128(sfLiquidityAmount));
    }
    else if (ctx_.tx.isFieldPresent(sfAmount) &&
             ctx_.tx.isFieldPresent(sfAmount2))
    {
        // Get pool's canonical asset ordering
        auto const& clammRefForOrder = std::as_const(*sleClamm);
        auto const poolIssue0 = clammRefForOrder[sfAsset].get<Issue>();
        auto const poolIssue1 = clammRefForOrder[sfAsset2].get<Issue>();
        auto const txIssue0 = ctx_.tx[sfAmount].issue();
        auto const txIssue1 = ctx_.tx[sfAmount2].issue();

        std::uint64_t amount0, amount1;
        if (txIssue0 == poolIssue0 && txIssue1 == poolIssue1)
        {
            amount0 = clamm::extractAmount(ctx_.tx[sfAmount]);
            amount1 = clamm::extractAmount(ctx_.tx[sfAmount2]);
        }
        else if (txIssue0 == poolIssue1 && txIssue1 == poolIssue0)
        {
            // User provided amounts in reverse order -- swap them
            amount0 = clamm::extractAmount(ctx_.tx[sfAmount2]);
            amount1 = clamm::extractAmount(ctx_.tx[sfAmount]);
        }
        else
        {
            JLOG(j_.debug())
                << "CLAMM Deposit: amount issues don't match pool assets.";
            return temBAD_AMOUNT;
        }

        liquidity = clamm::getLiquidityForAmounts(
            sqrtPriceCurrent, sqrtPriceLower, sqrtPriceUpper, amount0, amount1);
    }
    else
    {
        JLOG(j_.debug())
            << "CLAMM Deposit: must specify liquidity or token amounts.";
        return tecINSUFFICIENT_PAYMENT;
    }

    if (liquidity == 0)
    {
        JLOG(j_.debug()) << "CLAMM Deposit: zero liquidity.";
        return tecINSUFFICIENT_PAYMENT;
    }

    if (liquidity < clamm::uint128(CLAMM_MIN_LIQUIDITY))
    {
        JLOG(j_.debug()) << "CLAMM Deposit: liquidity below minimum threshold.";
        return tecINSUFFICIENT_PAYMENT;
    }

    // Check MinLiquidity constraint
    if (ctx_.tx.isFieldPresent(sfMinLiquidity))
    {
        auto const minLiquidity =
            clamm::fromSLEField(ctx_.tx.getFieldH128(sfMinLiquidity));
        if (liquidity < minLiquidity)
        {
            JLOG(j_.debug())
                << "CLAMM Deposit: computed liquidity below minimum.";
            return tecPATH_PARTIAL;
        }
    }

    // Calculate actual token amounts required
    std::uint64_t amount0Required = 0;
    std::uint64_t amount1Required = 0;

    if (sqrtPriceCurrent <= sqrtPriceLower)
    {
        amount0Required = clamm::getAmount0ForLiquidity(
            sqrtPriceLower, sqrtPriceUpper, liquidity);
    }
    else if (sqrtPriceCurrent < sqrtPriceUpper)
    {
        amount0Required = clamm::getAmount0ForLiquidity(
            sqrtPriceCurrent, sqrtPriceUpper, liquidity);
        amount1Required = clamm::getAmount1ForLiquidity(
            sqrtPriceLower, sqrtPriceCurrent, liquidity);
    }
    else
    {
        amount1Required = clamm::getAmount1ForLiquidity(
            sqrtPriceLower, sqrtPriceUpper, liquidity);
    }

    // Get pool assets
    auto const& clammRef = std::as_const(*sleClamm);
    auto const issue0 = clammRef[sfAsset].get<Issue>();
    auto const issue1 = clammRef[sfAsset2].get<Issue>();

    // Transfer tokens from user to pool
    if (amount0Required > 0)
    {
        auto const deposit0 = clamm::makeSTAmount(issue0, amount0Required);
        auto const res = accountSend(
            sb, account, ammAccountID, deposit0, j_, WaiveTransferFee::Yes);
        if (res != tesSUCCESS)
        {
            JLOG(j_.debug()) << "CLAMM Deposit: transfer token0 failed: "
                             << transHuman(res);
            return res;
        }
    }

    if (amount1Required > 0)
    {
        auto const deposit1 = clamm::makeSTAmount(issue1, amount1Required);
        auto const res = accountSend(
            sb, account, ammAccountID, deposit1, j_, WaiveTransferFee::Yes);
        if (res != tesSUCCESS)
        {
            JLOG(j_.debug()) << "CLAMM Deposit: transfer token1 failed: "
                             << transHuman(res);
            return res;
        }
    }

    // Update or create lower tick
    {
        auto const tickKeylet = keylet::clammTick(poolID, lowerTick);
        auto sleTick = sb.peek(tickKeylet);
        if (!sleTick)
        {
            sleTick = std::make_shared<SLE>(tickKeylet);
            sleTick->setFieldH256(sfPoolID, poolID);
            sleTick->setFieldI32(sfTickIndex, lowerTick);
            sleTick->setFieldH128(
                sfLiquidityGross, clamm::toSLEField(liquidity));
            sleTick->setFieldH128(
                sfLiquidityNet, clamm::toSLEFieldSigned(
                                    static_cast<clamm::int128>(liquidity)));

            // Snapshot current fee growth if tick is below current
            if (lowerTick <= currentTick)
            {
                auto const fg0 =
                    sleClamm->getFieldH128(sfFeeGrowthGlobal0);
                auto const fg1 =
                    sleClamm->getFieldH128(sfFeeGrowthGlobal1);
                if (fg0 != base_uint<128>{})
                    sleTick->setFieldH128(sfFeeGrowthOutside0, fg0);
                if (fg1 != base_uint<128>{})
                    sleTick->setFieldH128(sfFeeGrowthOutside1, fg1);
            }

            auto page = sb.dirInsert(
                keylet::ownerDir(ammAccountID),
                tickKeylet,
                describeOwnerDir(ammAccountID));
            if (!page)
                return tecDIR_FULL;
            sleTick->setFieldU64(sfOwnerNode, *page);
            sleTick->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
            sleTick->setFieldU32(
                sfPreviousTxnLgrSeq, ctx_.view().seq());
            sb.insert(sleTick);
            if (auto const ret = clamm::flipTickBitmap(
                    sb, poolID, ammAccountID, lowerTick, tickSpacing, j_);
                ret != tesSUCCESS)
                return ret;
        }
        else
        {
            auto gross =
                clamm::fromSLEField(sleTick->getFieldH128(sfLiquidityGross));
            auto net = clamm::fromSLEFieldSigned(
                sleTick->getFieldH128(sfLiquidityNet));
            gross += liquidity;
            net += static_cast<clamm::int128>(liquidity);
            sleTick->setFieldH128(
                sfLiquidityGross, clamm::toSLEField(gross));
            sleTick->setFieldH128(
                sfLiquidityNet, clamm::toSLEFieldSigned(net));
            sleTick->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
            sleTick->setFieldU32(
                sfPreviousTxnLgrSeq, ctx_.view().seq());
            sb.update(sleTick);
        }
    }

    // Update or create upper tick
    {
        auto const tickKeylet = keylet::clammTick(poolID, upperTick);
        auto sleTick = sb.peek(tickKeylet);
        if (!sleTick)
        {
            sleTick = std::make_shared<SLE>(tickKeylet);
            sleTick->setFieldH256(sfPoolID, poolID);
            sleTick->setFieldI32(sfTickIndex, upperTick);
            sleTick->setFieldH128(
                sfLiquidityGross, clamm::toSLEField(liquidity));
            sleTick->setFieldH128(
                sfLiquidityNet,
                clamm::toSLEFieldSigned(
                    -static_cast<clamm::int128>(liquidity)));

            if (upperTick <= currentTick)
            {
                auto const fg0 =
                    sleClamm->getFieldH128(sfFeeGrowthGlobal0);
                auto const fg1 =
                    sleClamm->getFieldH128(sfFeeGrowthGlobal1);
                if (fg0 != base_uint<128>{})
                    sleTick->setFieldH128(sfFeeGrowthOutside0, fg0);
                if (fg1 != base_uint<128>{})
                    sleTick->setFieldH128(sfFeeGrowthOutside1, fg1);
            }

            auto page = sb.dirInsert(
                keylet::ownerDir(ammAccountID),
                tickKeylet,
                describeOwnerDir(ammAccountID));
            if (!page)
                return tecDIR_FULL;
            sleTick->setFieldU64(sfOwnerNode, *page);
            sleTick->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
            sleTick->setFieldU32(
                sfPreviousTxnLgrSeq, ctx_.view().seq());
            sb.insert(sleTick);
            if (auto const ret = clamm::flipTickBitmap(
                    sb, poolID, ammAccountID, upperTick, tickSpacing, j_);
                ret != tesSUCCESS)
                return ret;
        }
        else
        {
            auto gross =
                clamm::fromSLEField(sleTick->getFieldH128(sfLiquidityGross));
            auto net = clamm::fromSLEFieldSigned(
                sleTick->getFieldH128(sfLiquidityNet));
            gross += liquidity;
            net -= static_cast<clamm::int128>(liquidity);
            sleTick->setFieldH128(
                sfLiquidityGross, clamm::toSLEField(gross));
            sleTick->setFieldH128(
                sfLiquidityNet, clamm::toSLEFieldSigned(net));
            sleTick->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
            sleTick->setFieldU32(
                sfPreviousTxnLgrSeq, ctx_.view().seq());
            sb.update(sleTick);
        }
    }

    // If position is in active range, update pool's active liquidity
    if (lowerTick <= currentTick && currentTick < upperTick)
    {
        auto poolLiquidity =
            clamm::fromSLEField(sleClamm->getFieldH128(sfLiquidityAmount));
        poolLiquidity += liquidity;
        sleClamm->setFieldH128(
            sfLiquidityAmount, clamm::toSLEField(poolLiquidity));
    }

    // Compute proper feeGrowthInside for the position's tick range
    auto const feeGrowthInside = clamm::computeFeeGrowthInside(
        sb, poolID, lowerTick, upperTick, currentTick,
        feeGrowthGlobal0, feeGrowthGlobal1);

    if (addToExisting)
    {
        // Add liquidity to existing position.
        // First, collect any accrued fees (update tokensOwed).
        auto const posLiquidity = clamm::fromSLEField(
            sleExistingPos->getFieldH128(sfLiquidityAmount));
        auto const fgi0Last = clamm::fromSLEField(
            sleExistingPos->getFieldH128(sfFeeGrowthInside0Last));
        auto const fgi1Last = clamm::fromSLEField(
            sleExistingPos->getFieldH128(sfFeeGrowthInside1Last));

        // Compute uncollected fees
        if (posLiquidity > 0)
        {
            auto const feeDelta0 = feeGrowthInside.feeGrowthInside0 - fgi0Last;
            auto const feeDelta1 = feeGrowthInside.feeGrowthInside1 - fgi1Last;

            auto tokensOwed0 = sleExistingPos->getFieldU64(sfTokensOwed0);
            auto tokensOwed1 = sleExistingPos->getFieldU64(sfTokensOwed1);

            {
                auto const a = static_cast<std::uint64_t>(
                    (clamm::uint256(posLiquidity) * clamm::uint256(feeDelta0)) >>
                    clamm::Q96);
                tokensOwed0 = (tokensOwed0 <= UINT64_MAX - a)
                    ? tokensOwed0 + a : UINT64_MAX;
            }
            {
                auto const a = static_cast<std::uint64_t>(
                    (clamm::uint256(posLiquidity) * clamm::uint256(feeDelta1)) >>
                    clamm::Q96);
                tokensOwed1 = (tokensOwed1 <= UINT64_MAX - a)
                    ? tokensOwed1 + a : UINT64_MAX;
            }

            sleExistingPos->setFieldU64(sfTokensOwed0, tokensOwed0);
            sleExistingPos->setFieldU64(sfTokensOwed1, tokensOwed1);
        }

        // Update position liquidity and fee growth snapshots
        auto const newLiquidity = posLiquidity + liquidity;
        sleExistingPos->setFieldH128(
            sfLiquidityAmount, clamm::toSLEField(newLiquidity));

        // Always update fee growth snapshot unconditionally
        sleExistingPos->setFieldH128(sfFeeGrowthInside0Last,
            clamm::toSLEField(feeGrowthInside.feeGrowthInside0));
        sleExistingPos->setFieldH128(sfFeeGrowthInside1Last,
            clamm::toSLEField(feeGrowthInside.feeGrowthInside1));

        sleExistingPos->setFieldH256(
            sfPreviousTxnID, ctx_.tx.getTransactionID());
        sleExistingPos->setFieldU32(
            sfPreviousTxnLgrSeq, ctx_.view().seq());
        sb.update(sleExistingPos);
    }
    else
    {
        // Create new position: mint NFToken and create CLAMMPosition SLE

        auto const sleAccount = sb.peek(keylet::account(account));
        if (!sleAccount)
            return tefINTERNAL;

        // Reserve pre-check: new position needs reserves for NFToken page,
        // CLAMMPosition, and potentially new tick SLEs (up to 3 objects)
        auto const ownerCount = sleAccount->getFieldU32(sfOwnerCount);
        auto const balance =
            std::as_const(*sleAccount)[sfBalance].xrp();
        auto const reserveNeeded = sb.fees().accountReserve(ownerCount + 3);
        if (balance < reserveNeeded)
        {
            JLOG(j_.debug())
                << "CLAMM Deposit: insufficient reserve for new position.";
            return tecINSUFFICIENT_RESERVE;
        }

        auto const tokenSeq = sleAccount->getFieldU32(sfMintedNFTokens);
        auto const nftokenID = NFTokenMint::createNFTokenID(
            nft::flagTransferable,
            0,
            account,
            nft::toTaxon(CLAMM_NFTOKEN_TAXON),
            tokenSeq);

        SOTemplate const* nfTokenTemplate =
            InnerObjectFormats::getInstance().findSOTemplateBySField(sfNFToken);
        if (!nfTokenTemplate)
            return tefINTERNAL;

        STObject newToken(
            *nfTokenTemplate,
            sfNFToken,
            [&nftokenID, &poolID, lowerTick, upperTick](STObject& object) {
                object.setFieldH256(sfNFTokenID, nftokenID);
                std::string uri = "{\"pool\":\"" + to_string(poolID) +
                    "\",\"lt\":" + std::to_string(lowerTick) +
                    ",\"ut\":" + std::to_string(upperTick) + "}";
                object.setFieldVL(
                    sfURI, Slice(uri.data(), uri.size()));
            });

        if (auto const ret =
                nft::insertToken(sb, account, std::move(newToken));
            ret != tesSUCCESS)
        {
            JLOG(j_.debug()) << "CLAMM Deposit: NFToken mint failed: "
                             << transHuman(ret);
            return ret;
        }

        sleAccount->setFieldU32(sfMintedNFTokens, tokenSeq + 1);
        sb.update(sleAccount);

        // Create CLAMMPosition ledger entry
        auto const posKeylet = keylet::clammPosition(nftokenID);
        auto slePos = std::make_shared<SLE>(posKeylet);
        slePos->setFieldH256(sfPoolID, poolID);
        slePos->setFieldH256(sfNFTokenID, nftokenID);
        slePos->setAccountID(sfOwner, account);
        slePos->setFieldI32(sfLowerTick, lowerTick);
        slePos->setFieldI32(sfUpperTick, upperTick);
        slePos->setFieldH128(sfLiquidityAmount, clamm::toSLEField(liquidity));

        // Snapshot feeGrowthInside unconditionally
        slePos->setFieldH128(sfFeeGrowthInside0Last,
            clamm::toSLEField(feeGrowthInside.feeGrowthInside0));
        slePos->setFieldH128(sfFeeGrowthInside1Last,
            clamm::toSLEField(feeGrowthInside.feeGrowthInside1));

        auto page = sb.dirInsert(
            keylet::ownerDir(account),
            posKeylet,
            describeOwnerDir(account));
        if (!page)
            return tecDIR_FULL;
        slePos->setFieldU64(sfOwnerNode, *page);
        slePos->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
        slePos->setFieldU32(sfPreviousTxnLgrSeq, ctx_.view().seq());
        sb.insert(slePos);
    }

    // Update pool's previous txn tracking
    sleClamm->setFieldH256(sfPreviousTxnID, ctx_.tx.getTransactionID());
    sleClamm->setFieldU32(sfPreviousTxnLgrSeq, ctx_.view().seq());
    sb.update(sleClamm);

    sb.apply(ctx_.rawView());
    return tesSUCCESS;
}

}  // namespace xrpl
