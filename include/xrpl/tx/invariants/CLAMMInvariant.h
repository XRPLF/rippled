#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>

#include <cstdint>
#include <optional>

namespace xrpl {

class ValidCLAMM
{
    bool clammCreated_{false};
    bool clammModified_{false};
    bool clammDeleted_{false};
    bool clammTickChanged_{false};
    bool clammPositionChanged_{false};
    std::uint32_t clammTicksCreated_{0};
    std::uint32_t clammTicksDeleted_{0};
    std::uint32_t clammPositionsCreated_{0};
    std::uint32_t clammPositionsDeleted_{0};

    // Safety-net: captured from after-SLE for key field validation
    std::optional<std::uint8_t> clammFeeTier_;
    bool clammSqrtPriceZero_{false};

    // Value-level invariant fields
    std::optional<base_uint<128>> clammSqrtPriceAfter_;
    std::optional<std::int32_t> clammCurrentTickAfter_;
    std::optional<std::uint16_t> clammTickSpacing_;
    bool clammTickMisaligned_{false};
    bool clammPositionBadBounds_{false};
    bool clammTickLiquidityZero_{false};
    bool clammSqrtPriceTickMismatch_{false};
    bool clammTickBitmapChanged_{false};

public:
    void
    visitEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after);

    bool
    finalize(
        STTx const& tx,
        TER const tec,
        XRPAmount const fee,
        ReadView const& view,
        beast::Journal const& j);

private:
    bool
    finalizeCreate(
        STTx const& tx,
        ReadView const& view,
        beast::Journal const& j) const;
    bool
    finalizeDeposit(
        STTx const& tx,
        ReadView const& view,
        beast::Journal const& j) const;
    bool
    finalizeWithdraw(
        STTx const& tx,
        ReadView const& view,
        beast::Journal const& j) const;
    bool
    finalizeSwap(
        STTx const& tx,
        ReadView const& view,
        beast::Journal const& j) const;
    bool
    finalizeCollectFees(
        STTx const& tx,
        ReadView const& view,
        beast::Journal const& j) const;
    bool
    finalizeVote(
        STTx const& tx,
        ReadView const& view,
        beast::Journal const& j) const;
    bool
    finalizeBid(
        STTx const& tx,
        ReadView const& view,
        beast::Journal const& j) const;
    bool
    finalizeDelete(
        STTx const& tx,
        ReadView const& view,
        beast::Journal const& j) const;
    bool
    finalizeClawback(
        STTx const& tx,
        ReadView const& view,
        beast::Journal const& j) const;

    /** Validate value-level invariants on CLAMM pool state.
     *  Called from finalize methods (except delete).
     */
    bool
    validateValues(beast::Journal const& j) const;
};

}  // namespace xrpl
