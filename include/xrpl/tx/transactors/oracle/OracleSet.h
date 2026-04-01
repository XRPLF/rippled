#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
    Price Oracle is a system that acts as a bridge between
    a blockchain network and the external world, providing off-chain price data
    to decentralized applications (dApps) on the blockchain. This implementation
    conforms to the requirements specified in the XLS-47d.

    The OracleSet transactor implements creating or updating Oracle objects.
*/

class OracleSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit OracleSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl
