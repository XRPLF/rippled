#ifndef XRPL_TX_SETORACLE_H_INCLUDED
#define XRPL_TX_SETORACLE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

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
};

using OracleSet = SetOracle;

}  // namespace ripple

#endif  // XRPL_TX_SETORACLE_H_INCLUDED
