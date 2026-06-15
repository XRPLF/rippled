#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * @brief Allows an MPT issuer to clawback confidential balances from a holder.
 *
 * This transaction enables the issuer of an MPToken Issuance (with clawback
 * enabled) to reclaim confidential tokens from a holder's account. Unlike
 * regular clawback, the issuer cannot see the holder's balance directly.
 * Instead, the issuer must provide a zero-knowledge proof that demonstrates
 * they know the exact encrypted balance amount.
 *
 * @par Cryptographic Operations:
 * - **Equality Proof Verification**: Verifies that the issuer's revealed
 *   amount matches the holder's encrypted balance using the issuer's
 *   ElGamal private key.
 *
 * @see ConfidentialMPTSend, ConfidentialMPTConvert
 */
class ConfidentialMPTClawback : public Transactor
{
public:
    static constexpr auto kConsequencesFactory = ConsequencesFactoryType::Normal;

    explicit ConfidentialMPTClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static XRPAmount
    calculateBaseFee(ReadView const& view, STTx const& tx);

    static TER
    preclaim(PreclaimContext const& ctx);

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
};

}  // namespace xrpl
