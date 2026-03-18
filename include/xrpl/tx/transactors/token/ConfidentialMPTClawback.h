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
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTClawback(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates transaction format and cryptographic field structure.
     *  @see §11.3.1
     * https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0096-confidential-mpt#1131-data-verification
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates ledger state and cryptographic proofs.
     *  @see §11.3.2
     * https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0096-confidential-mpt#1132-protocol-level-failures
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Updates ledger state.
     *  @see §11.4
     * https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0096-confidential-mpt#114-state-changes
     */
    TER
    doApply() override;
};

}  // namespace xrpl
