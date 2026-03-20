#pragma once

#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Quality.h>
#include <xrpl/protocol/QualityFunction.h>
#include <xrpl/protocol/TER.h>
#include <xrpld/app/paths/CLAMMLiquidity.h>

namespace xrpl {

template <typename TIn, typename TOut>
class CLAMMLiquidity;

/** Represents synthetic CLAMM offer in BookStep. CLAMMOffer mirrors
 * TOffer/AMMOffer methods for use in generic BookStep methods.
 */
template <typename TIn, typename TOut>
class CLAMMOffer
{
private:
    CLAMMLiquidity<TIn, TOut> const& clammLiquidity_;
    CLAMMPoolInfo pool_;
    TAmounts<TIn, TOut> amounts_;
    Quality const quality_;
    beast::Journal const j_;
    bool consumed_;

public:
    CLAMMOffer(
        CLAMMLiquidity<TIn, TOut> const& clammLiquidity,
        CLAMMPoolInfo const& pool,
        TAmounts<TIn, TOut> const& amounts,
        Quality const& quality,
        beast::Journal j);

    Quality
    quality() const noexcept
    {
        return quality_;
    }

    Issue const&
    issueIn() const
    {
        return clammLiquidity_.issueIn();
    }

    AccountID const&
    owner() const
    {
        return pool_.ammAccountID;
    }

    std::optional<uint256>
    key() const
    {
        return std::nullopt;
    }

    TAmounts<TIn, TOut> const&
    amount() const
    {
        return amounts_;
    }

    void
    consume(ApplyView& view, TAmounts<TIn, TOut> const& consumed);

    bool
    fully_consumed() const
    {
        return consumed_;
    }

    /** Limit out of the provided offer.
     * Uses constant quality approximation for simplicity.
     */
    TAmounts<TIn, TOut>
    limitOut(
        TAmounts<TIn, TOut> const& offerAmount,
        TOut const& limit,
        bool roundUp) const;

    /** Limit in of the provided offer.
     * Uses constant quality approximation for simplicity.
     */
    TAmounts<TIn, TOut>
    limitIn(
        TAmounts<TIn, TOut> const& offerAmount,
        TIn const& limit,
        bool roundUp) const;

    QualityFunction
    getQualityFunc() const;

    /** Send funds without incurring the transfer fee. */
    template <typename... Args>
    static TER
    send(Args&&... args)
    {
        return accountSend(
            std::forward<Args>(args)..., WaiveTransferFee::Yes);
    }

    bool
    isFunded() const
    {
        return true;
    }

    static std::pair<std::uint32_t, std::uint32_t>
    adjustRates(std::uint32_t ofrInRate, std::uint32_t /*ofrOutRate*/)
    {
        // CLAMM doesn't pay transfer fee on output, matching XLS-30 AMM
        // behavior (AMMOffer::adjustRates). Pool-held assets are not subject
        // to issuer transfer fees on outbound transfers during payment routing.
        return {ofrInRate, QUALITY_ONE};
    }

    /** Verify consumed amounts are within expected bounds. */
    bool
    checkInvariant(
        TAmounts<TIn, TOut> const& consumed,
        beast::Journal j) const;
};

}  // namespace xrpl
