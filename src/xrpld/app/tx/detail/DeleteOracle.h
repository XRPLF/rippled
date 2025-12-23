#ifndef XRPL_TX_DELETEORACLE_H_INCLUDED
#define XRPL_TX_DELETEORACLE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

/**
    Price Oracle is a system that acts as a bridge between
    a blockchain network and the external world, providing off-chain price data
    to decentralized applications (dApps) on the blockchain. This implementation
    conforms to the requirements specified in the XLS-47d.

    The DeleteOracle transactor implements the deletion of Oracle objects.
*/

class DeleteOracle : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit DeleteOracle(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    static TER
    deleteOracle(
        ApplyView& view,
        std::shared_ptr<SLE> const& sle,
        AccountID const& account,
        beast::Journal j);
};

using OracleDelete = DeleteOracle;

}  // namespace ripple

#endif  // XRPL_TX_DELETEORACLE_H_INCLUDED
