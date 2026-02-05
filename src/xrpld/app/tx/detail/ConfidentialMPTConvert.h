#ifndef XRPL_TX_CONFIDENTIALCONVERT_H_INCLUDED
#define XRPL_TX_CONFIDENTIALCONVERT_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

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
 * @par Balance Updates:
 * - Decreases the holder's public `MPTAmount` balance.
 * - Increases the holder's encrypted balances (inbox or spending).
 * - Increases the global `ConfidentialOutstandingAmount` on the MPT Issuance.
 * - If this is the first conversion, initializes all confidential balance
 *   fields and sets spending balance to encrypted zero.
 *
 * @par Requirements:
 * - The MPT Issuance must have `lsfMPTCanPrivacy` flag set.
 * - The issuer must have registered their ElGamal public key.
 * - Holder must have sufficient public balance to convert.
 * - If auditing is enabled on the issuance, auditor ciphertext must be
 *   provided.
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

    /**
     * @brief Validates transaction fields before any ledger access.
     *
     * Checks:
     * - Feature `featureConfidentialTransfer` is enabled.
     * - Sender is not the MPT issuer.
     * - Amount is within valid bounds.
     * - Blinding factor is 32 bytes.
     * - If registering new ElGamal public key, verifies key length and
     *   requires Schnorr proof.
     * - Encrypted amount formats are valid.
     *
     * @param ctx The preflight context containing transaction and rules.
     * @return tesSUCCESS if valid, or an appropriate error code.
     */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /**
     * @brief Validates transaction against current ledger state.
     *
     * Checks:
     * - MPT Issuance exists and has privacy enabled.
     * - Issuer has registered ElGamal public key.
     * - Auditor requirements match (present if and only if required).
     * - Holder's MPToken exists with sufficient public balance.
     * - ElGamal public key registration rules (new key or existing).
     * - Schnorr proof for new key registration.
     * - Revealed amount proof verification.
     *
     * @param ctx The preclaim context containing view and transaction.
     * @return tesSUCCESS if valid, or an appropriate error code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * @brief Applies the conversion to the ledger.
     *
     * - Registers holder's ElGamal public key if provided.
     * - Decreases public balance, increases confidential outstanding amount.
     * - Homomorphically adds encrypted amounts to existing balances, or
     *   initializes confidential fields if this is the first conversion.
     *
     * @return tesSUCCESS on success, or an appropriate error code.
     */
    TER
    doApply() override;
};

}  // namespace xrpl

#endif
