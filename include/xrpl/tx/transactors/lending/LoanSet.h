#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <vector>

namespace xrpl {

class LoanSet : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit LoanSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    checkSign(PreclaimContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static std::vector<OptionaledField<STNumber>> const&
    getValueFields();

    static TER
    preclaim(PreclaimContext const& ctx);

    /** CollateralLock: checks that an escrow can be pledged to a new loan.

        The escrow must be a token escrow owned by and payable to the borrower,
        not already pledged, with a CancelAfter no earlier than releaseFloor
        (the loan's maturity plus one more grace period), and the broker owner
        must pass the checks an EscrowFinish destination must pass.
    */
    static TER
    checkCollateral(
        ReadView const& view,
        uint256 const& collateralID,
        AccountID const& borrower,
        AccountID const& brokerOwner,
        std::uint64_t releaseFloor,
        beast::Journal j);

    TER
    doApply() override;

    void
    visitInvariantEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

public:
    static constexpr std::uint32_t kMinPaymentTotal = 1;
    static constexpr std::uint32_t kDefaultPaymentTotal = 1;
    static_assert(kDefaultPaymentTotal >= kMinPaymentTotal);

    static constexpr std::uint32_t kMinPaymentInterval = 60;
    static constexpr std::uint32_t kDefaultPaymentInterval = 60;
    static_assert(kDefaultPaymentInterval >= kMinPaymentInterval);

    static constexpr std::uint32_t kDefaultGracePeriod = 60;
    static_assert(kDefaultGracePeriod >= kMinPaymentInterval);
};

//------------------------------------------------------------------------------

}  // namespace xrpl
