#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>
#include <optional>
#include <set>

namespace xrpl {

class SponsoredAccountCreate : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Custom;

    explicit SponsoredAccountCreate(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    static bool
    checkExtraFeatures(PreflightContext const& ctx);

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    void
    visitInvariantEntry(bool isDelete, SLE::ConstRef before, SLE::ConstRef after) override;

    [[nodiscard]] bool
    finalizeInvariants(
        STTx const& tx,
        TER result,
        XRPAmount fee,
        ReadView const& view,
        beast::Journal const& j) override;

private:
    // INVARIANTS

    // Keyed by entry so repeated invariant runs do not double count.
    std::set<uint256> accountsCreated_;
    std::set<uint256> sponsorshipsCreated_;
    std::set<AccountID> otherAccountsModified_;
    std::int64_t sponsoringAccountCountDelta_ = 0;
    bool nonZeroBalance_ = false;
    bool unexpectedEntry_ = false;
    std::optional<AccountID> newAccountID_;
    std::optional<AccountID> newAccountSponsor_;
    std::optional<AccountID> proposalOwner_;
    std::optional<AccountID> proposalReserveSponsor_;
    SLE::const_pointer newSponsorship_;
};

}  // namespace xrpl
