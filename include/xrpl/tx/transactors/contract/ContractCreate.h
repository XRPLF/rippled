#pragma once

#include <xrpl/protocol/Fees.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

class ContractCreate : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit ContractCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * Checks a transaction's `ContractCode` against the limits in `fees`, then screens the
     * module for each function its `Functions` names. Shared by ContractCreate and
     * ContractModify.
     */
    static TER
    checkContractCode(Fees const& fees, STTx const& tx, beast::Journal j);

    /**
     * The hash of the ContractSource a ContractCreate or ContractModify names: the hash of its
     * `ContractCode`, else its `ContractHash`. Preflight has checked that one of them is present.
     */
    static uint256
    sourceHash(STTx const& tx);

    TER
    doApply() override;

    void
    visitInvariantEntry(bool isDelete, SLE::const_ref before, SLE::const_ref after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;
};

}  // namespace xrpl
