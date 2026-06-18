#pragma once

#include <xrpl/ledger/helpers/NFTokenHelpers.h>
#include <xrpl/protocol/nft.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

class NFTokenMint : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit NFTokenMint(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

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

    // Public to support unit tests.
    static uint256
    createNFTokenID(
        std::uint16_t flags,
        std::uint16_t fee,
        AccountID const& issuer,
        nft::Taxon taxon,
        std::uint32_t tokenSeq);
};

}  // namespace xrpl
