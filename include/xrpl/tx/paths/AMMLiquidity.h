#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace xrpl {

template <StepAmount TIn, StepAmount TOut>
class AMMOffer;

/**
 * AMMLiquidity class provides AMM offers to BookStep class.
 * The offers are generated in two ways. If there are multiple
 * paths specified to the payment transaction then the offers
 * are generated based on the Fibonacci sequence with
 * a limited number of payment engine iterations consuming AMM offers.
 * These offers behave the same way as CLOB offers in that if
 * there is a limiting step, then the offers are adjusted
 * based on their quality.
 * If there is only one path specified in the payment transaction
 * then the offers are generated based on the competing CLOB offer
 * quality. In this case the offer's size is set in such a way
 * that the new AMM's pool spot price quality is equal to the CLOB's
 * offer quality.
 */
template <typename TIn, typename TOut>
class AMMLiquidity
{
private:
    inline static Number const kInitialFibSeqPct = Number(5) / 20000;
    AMMContext& ammContext_;
    AccountID const ammAccountID_;
    std::uint32_t const tradingFee_;
    Asset const assetIn_;
    Asset const assetOut_;
    // Initial AMM pool balances
    TAmounts<TIn, TOut> const initialBalances_;
    beast::Journal const j_;
    std::uint8_t const curveType_{kCtConstantProduct};
    uint256 const ammID_;

public:
    AMMLiquidity(
        ReadView const& view,
        AccountID const& ammAccountID,
        std::uint32_t tradingFee,
        Asset const& in,
        Asset const& out,
        AMMContext& ammContext,
        beast::Journal j,
        std::uint8_t curveType = kCtConstantProduct);
    ~AMMLiquidity() = default;
    AMMLiquidity(AMMLiquidity const&) = delete;
    AMMLiquidity&
    operator=(AMMLiquidity const&) = delete;

    /**
     * Generate AMM offer. Returns nullopt if clobQuality is provided
     * and it is better than AMM offer quality. Otherwise returns AMM offer.
     * If clobQuality is provided then AMM offer size is set based on the
     * quality.
     */
    [[nodiscard]] std::optional<AMMOffer<TIn, TOut>>
    getOffer(ReadView const& view, std::optional<Quality> const& clobQuality) const;

    [[nodiscard]] AccountID const&
    ammAccount() const
    {
        return ammAccountID_;
    }

    [[nodiscard]] bool
    multiPath() const
    {
        return ammContext_.multiPath();
    }

    [[nodiscard]] std::uint32_t
    tradingFee() const
    {
        return tradingFee_;
    }

    [[nodiscard]] AMMContext&
    context() const
    {
        return ammContext_;
    }

    [[nodiscard]] Asset const&
    assetIn() const
    {
        return assetIn_;
    }

    [[nodiscard]] Asset const&
    assetOut() const
    {
        return assetOut_;
    }

    [[nodiscard]] std::uint8_t
    curveType() const
    {
        return curveType_;
    }

    /**
     * The AMM entry as `view` holds it now, whose curve fields (sfCurrentTick,
     * sfActiveLiquidity, sfActiveBinID, sfAmplification...) price the next
     * offer; nullptr for constant product, whose math reads none. For a CL
     * pool the price, tick and liquidity are those of the range the next offer
     * draws from, past the ticks and empty ranges a swap crosses for free.
     */
    [[nodiscard]] std::shared_ptr<SLE const>
    curveParams(ReadView const& view) const;

    [[nodiscard]] uint256 const&
    ammID() const
    {
        return ammID_;
    }

private:
    /**
     * Fetches current AMM balances.
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    fetchBalances(ReadView const& view) const;

    /**
     * The virtual reserves of a CL pool's current range (liquidity / sqrtP of
     * token0, liquidity * sqrtP of token1), as in/out; nullopt for another
     * curve, a range with no liquidity, or reserves the amount types cannot hold.
     */
    [[nodiscard]] std::optional<TAmounts<TIn, TOut>>
    clRangeReserves(ReadView const& view) const;

    /**
     * Generate AMM offers with the offer size based on Fibonacci sequence.
     * The sequence corresponds to the payment engine iterations with AMM
     * liquidity. Iterations that don't consume AMM offers don't count.
     * The number of iterations with AMM offers is limited.
     * If the generated offer exceeds the pool balance then the function
     * throws overflow exception.
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    generateFibSeqOffer(ReadView const& view, TAmounts<TIn, TOut> const& balances) const;

    /**
     * Generate max offer. The offer is generated as:
     * takerGets = 99% * balances.out takerPays = swapOut(takerGets).
     * Return nullopt if takerGets is 0 or takerGets == balances.out.
     */
    [[nodiscard]] std::optional<AMMOffer<TIn, TOut>>
    maxOffer(ReadView const& view, TAmounts<TIn, TOut> const& balances, Rules const& rules) const;
};

}  // namespace xrpl
