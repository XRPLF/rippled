#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/Transactor.h>

namespace xrpl {

struct MPTCreateArgs
{
    std::optional<XRPAmount> priorBalance;
    AccountID const& account;
    std::uint32_t sequence = 0;
    std::uint32_t flags = 0;
    std::optional<std::uint64_t> maxAmount =
        std::nullopt;  // NOLINT(readability-redundant-member-init)
    std::optional<std::uint8_t> assetScale =
        std::nullopt;  // NOLINT(readability-redundant-member-init)
    std::optional<std::uint16_t> transferFee =
        std::nullopt;  // NOLINT(readability-redundant-member-init)
    std::optional<Slice> const& metadata{};
    std::optional<uint256> domainId = std::nullopt;  // NOLINT(readability-redundant-member-init)
    std::optional<std::uint32_t> mutableFlags =
        std::nullopt;  // NOLINT(readability-redundant-member-init)
};

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
    visitInvariantEntry(
        bool isDelete,
        std::shared_ptr<SLE const> const& before,
        std::shared_ptr<SLE const> const& after) override;

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
