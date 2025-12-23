#ifndef XRPL_TX_NFTTOKENMINT_H_INCLUDED
#define XRPL_TX_NFTTOKENMINT_H_INCLUDED

#include <xrpld/app/tx/detail/NFTokenUtils.h>
#include <xrpld/app/tx/detail/Transactor.h>

#include <xrpl/protocol/nft.h>

namespace ripple {

class NFTokenMint : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

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

    // Public to support unit tests.
    static uint256
    createNFTokenID(
        std::uint16_t flags,
        std::uint16_t fee,
        AccountID const& issuer,
        nft::Taxon taxon,
        std::uint32_t tokenSeq);
};

}  // namespace ripple

#endif
