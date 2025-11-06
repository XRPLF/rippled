#ifndef XRPL_TX_DID_H_INCLUDED
#define XRPL_TX_DID_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

class DIDSet : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit DIDSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    TER
    doApply() override;
};

//------------------------------------------------------------------------------

class DIDDelete : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit DIDDelete(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    deleteSLE(ApplyContext& ctx, Keylet sleKeylet, AccountID const owner);

    static TER
    deleteSLE(
        ApplyView& view,
        std::shared_ptr<SLE> sle,
        AccountID const owner,
        beast::Journal j);

    TER
    doApply() override;
};

}  // namespace ripple

#endif
