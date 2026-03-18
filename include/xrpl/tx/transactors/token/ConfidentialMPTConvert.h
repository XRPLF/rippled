#pragma once

#include <xrpl/tx/Transactor.h>

namespace xrpl {

/**
 * @brief Converts public (plaintext) MPT balance to confidential (encrypted)
 * balance.
 *
 * This transaction allows a token holder to convert their publicly visible
 * MPToken balance into an encrypted confidential balance. Once converted,
 * the balance can only be spent using ConfidentialMPTSend transactions and
 * remains hidden from public view on the ledger.
 *
 * @par Cryptographic Operations:
 * - **Schnorr Proof Verification**: When registering a new ElGamal public key,
 *   verifies proof of knowledge of the corresponding private key.
 * - **Revealed Amount Verification**: Verifies that the provided encrypted
 *   amounts (for holder, issuer, and optionally auditor) all encrypt the
 *   same plaintext amount using the provided blinding factor.
 *
 * @see ConfidentialMPTConvertBack, ConfidentialMPTSend
 */
class ConfidentialMPTConvert : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTConvert(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /** Validates transaction format and cryptographic field structure.
     *  @see §7.3.1
     * https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0096-confidential-mpt#731-data-verification
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Validates ledger state and cryptographic proofs.
     *  @see §7.3.2
     * https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0096-confidential-mpt#732-protocol-level-failures
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Updates ledger state.
     *  @see §7.4
     * https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0096-confidential-mpt#74-state-changes
     */
    TER
    doApply() override;
};

}  // namespace xrpl
