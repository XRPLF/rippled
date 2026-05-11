#pragma once

#include <xrpl/basics/Log.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

namespace xrpl {

template <StepAmount TIn, StepAmount TOut>
class AMMOffer;

/** AMMLiquidity class provides AMM offers to BookStep class.
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

public:
    AMMLiquidity(
        ReadView const& view,
        AccountID const& ammAccountID,
        std::uint32_t tradingFee,
        Asset const& in,
        Asset const& out,
        AMMContext& ammContext,
        beast::Journal j);
    ~AMMLiquidity() = default;
    AMMLiquidity(AMMLiquidity const&) = delete;
    AMMLiquidity&
    operator=(AMMLiquidity const&) = delete;

    /** Generate AMM offer. Returns nullopt if clobQuality is provided
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

private:
    /** Fetches current AMM balances.
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    fetchBalances(ReadView const& view) const;

    /** Generate AMM offers with the offer size based on Fibonacci sequence.
     * The sequence corresponds to the payment engine iterations with AMM
     * liquidity. Iterations that don't consume AMM offers don't count.
     * The number of iterations with AMM offers is limited.
     * If the generated offer exceeds the pool balance then the function
     * throws overflow exception.
     */
    [[nodiscard]] TAmounts<TIn, TOut>
    generateFibSeqOffer(TAmounts<TIn, TOut> const& balances) const;

    /** Generate max offer.
     * If `fixAMMOverflowOffer` is active, the offer is generated as:
     * takerGets = 99% * balances.out takerPays = swapOut(takerGets).
     * Return nullopt if takerGets is 0 or takerGets == balances.out.
     *
     * If `fixAMMOverflowOffer` is not active, the offer is generated as:
     * takerPays = max input amount;
     * takerGets = swapIn(takerPays).
     */
    [[nodiscard]] std::optional<AMMOffer<TIn, TOut>>
    maxOffer(TAmounts<TIn, TOut> const& balances, Rules const& rules) const;
};

}  // namespace xrpl
