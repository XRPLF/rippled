#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
    Price Oracle is a system that acts as a bridge between
    a blockchain network and the external world, providing off-chain price data
    to decentralized applications (dApps) on the blockchain. This implementation
    conforms to the requirements specified in the XLS-47d.

    The SetOracle transactor implements creating or updating Oracle objects.
*/

class SetOracle : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit SetOracle(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

using OracleSet = SetOracle;

}  // namespace xrpl
