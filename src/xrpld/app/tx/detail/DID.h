#pragma once

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

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
    deleteSLE(ApplyView& view, std::shared_ptr<SLE> sle, AccountID const owner, beast::Journal j);

    TER
    doApply() override;
};

}  // namespace xrpl
