#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

// NOLINTBEGIN(readability-redundant-member-init)
struct MPTCreateArgs
{
    std::optional<XRPAmount> priorBalance;
    AccountID const& account;
    std::uint32_t sequence = 0;
    std::uint32_t flags = 0;
    std::optional<std::uint64_t> maxAmount = std::nullopt;
    std::optional<std::uint8_t> assetScale = std::nullopt;
    std::optional<std::uint16_t> transferFee = std::nullopt;
    std::optional<Slice> const& metadata{};
    std::optional<uint256> domainId = std::nullopt;
    std::optional<std::uint32_t> mutableFlags = std::nullopt;
    // Set only by callers that issue an MPT representing a wrapped asset
    // (e.g. VaultCreate's share token). The keylet must point to an
    // existing MPToken or RippleState owned by `account`. Surfaces on
    // the resulting MPTokenIssuance via the optional sfReferenceHolding
    // field. Used by readers (canTransfer, canTrade, freezing) to
    // inherit the underlying asset's transferability.
    std::optional<uint256> referenceHolding = std::nullopt;
};
// NOLINTEND(readability-redundant-member-init)

class MPTokenIssuanceCreate : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit MPTokenIssuanceCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

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

    static Expected<MPTID, TER>
    create(ApplyView& view, beast::Journal journal, MPTCreateArgs const& args);
};

}  // namespace xrpl
