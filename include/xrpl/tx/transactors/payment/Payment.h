#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

class Payment : public Transactor
{
    /* The largest number of paths we allow */
    static std::size_t const MaxPathSize = 6;

    /* The longest path we allow */
    static std::size_t const MaxPathLength = 8;

public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit Payment(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static NotTEC
    checkPermission(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl
