#pragma once

#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/tx/SignerEntries.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <vector>

namespace xrpl {

/**
See the README.md for an overview of the SignerListSet transaction that
this class implements.
*/
class SignerListSet : public Transactor
{
private:
    // Values determined during preCompute for use later.
    enum class Operation { unknown, set, destroy };
    Operation do_{Operation::unknown};
    std::uint32_t quorum_{0};
    std::vector<SignerEntries::SignerEntry> signers_;

public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Blocker};

    explicit SignerListSet(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    TER
    doApply() override;
    void
    preCompute() override;

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

    // Interface used by AccountDelete
    static TER
    removeFromLedger(
        ServiceRegistry& registry,
        ApplyView& view,
        AccountID const& account,
        beast::Journal j);

private:
    static std::tuple<NotTEC, std::uint32_t, std::vector<SignerEntries::SignerEntry>, Operation>
    determineOperation(STTx const& tx, ApplyFlags flags, beast::Journal j);

    static NotTEC
    validateQuorumAndSignerEntries(
        std::uint32_t quorum,
        std::vector<SignerEntries::SignerEntry> const& signers,
        AccountID const& account,
        beast::Journal j,
        Rules const&);

    TER
    replaceSignerList();
    TER
    destroySignerList();

    void
    writeSignersToSLE(SLE::pointer const& ledgerEntry, std::uint32_t flags) const;
};

}  // namespace xrpl
