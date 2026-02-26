#pragma once

#include <xrpld/app/tx/detail/Transactor.h>

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
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;
};

}  // namespace xrpl
