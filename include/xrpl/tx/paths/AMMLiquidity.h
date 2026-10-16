#pragma once

#include <xrpl/basics/Number.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/ledger/helpers/AMMHelpers.h>
#include <xrpl/protocol/AMMCore.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

#include <cstdint>
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
    std::uint8_t const curveType_{CtConstantProduct};
    // Borrows the AMM SLE rather than copying it. The SLE lives in the
    // owning ReadView's cache and is guaranteed to outlive this
    // AMMLiquidity (which is a per-strand member of BookStep, which is
    // per-payment, which holds the view). Avoiding the copy reclaims a
    // full STObject clone per non-CP curve at BookStep construction
    // (audit perf plan AMM-6). nullptr for CP — its swap math doesn't
    // consult the SLE.
    std::shared_ptr<SLE const> const ammSle_;
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
        std::uint8_t curveType = CtConstantProduct,
        std::shared_ptr<SLE const> ammSle = nullptr);
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

    [[nodiscard]] STObject const*
    curveParams() const
    {
        // The AMM SLE doubles as the curve params container — every
        // per-curve field (sfFeeTier, sfAmplification, sfActiveLiquidity,
        // sfCurrentTick, etc.) lives on it. Return a borrow.
        return ammSle_ ? static_cast<STObject const*>(ammSle_.get()) : nullptr;
    }

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
