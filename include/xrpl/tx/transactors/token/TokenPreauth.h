#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>

#include <cstdint>

namespace xrpl {

/**
 * Lets an MPT issuer pre-authorize a single holder for one issuance, or remove
 * that pre-authorization (XLS-87 Token Pre-Authorization).
 *
 * A pre-authorization is recorded as an issuer-owned TokenPreauth ledger
 * entry keyed by (holder, issuance). It can not coexist with a TokenBlock
 * entry for the same key (see TokenBlock): a blocked holder can not be
 * pre-authorized.
 *
 * The two ledger entry types share their layout and lifecycle, so the helpers
 * that create and remove them live here and are reused by TokenBlock and
 * AccountDelete.
 */
class TokenPreauth : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit TokenPreauth(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static std::uint32_t
    getFlagsMask(PreflightContext const& ctx);

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

    // Creates a TokenPreauth or TokenBlock entry owned by the transaction's
    // Account, for the transaction's Holder and MPTokenIssuanceID.
    static TER
    createEntry(ApplyContext& ctx, XRPAmount preFeeBalance, Keylet const& keylet, beast::Journal j);

    // Removes a TokenPreauth or TokenBlock entry. Also used by AccountDelete.
    static TER
    removeFromLedger(ApplyView& view, SLE::Ref sle, beast::Journal j);
};

}  // namespace xrpl
