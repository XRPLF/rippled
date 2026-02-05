#ifndef XRPL_TX_CONFIDENTIALSEND_H_INCLUDED
#define XRPL_TX_CONFIDENTIALSEND_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace xrpl {

/**
 * @brief Transfers confidential MPT tokens between holders privately.
 *
 * This transaction enables private token transfers where the transfer amount
 * is hidden from public view. Both sender and recipient must have initialized
 * confidential balances. The transaction provides encrypted amounts for all
 * parties (sender, destination, issuer, and optionally auditor) along with
 * zero-knowledge proofs that verify correctness without revealing the amount.
 *
 * @par Cryptographic Operations:
 * - **Multi-Ciphertext Equality Proof**: Verifies that all encrypted amounts
 *   (sender, destination, issuer, auditor) encrypt the same plaintext value.
 * - **Amount Pedersen Linkage Proof**: Verifies that the amount commitment
 *   correctly links to the sender's encrypted amount.
 * - **Balance Pedersen Linkage Proof**: Verifies that the balance commitment
 *   correctly links to the sender's encrypted spending balance.
 * - **Bulletproof Range Proof**: Verifies remaining balance and
 *   transfer amount are non-negative.
 *
 * @par Balance Updates:
 * - **Sender**: Homomorphically subtracts from spending balance and issuer/
 *   auditor encrypted balances.
 * - **Destination**: Homomorphically adds to inbox balance and issuer/auditor
 *   encrypted balances.
 * - Increments both sender's and destination's `ConfidentialBalanceVersion`.
 *
 * @par Requirements:
 * - The MPTokenIssuance must have `lsfMPTCanTransfer` and `lsfMPTCanPrivacy`
 *   flags set.
 * - The issuer must have registered their ElGamal public key.
 * - Both sender and destination must have initialized confidential balances.
 * - Neither sender nor destination can be frozen or unauthorized.
 * - If auditing is enabled, auditor ciphertext must be provided.
 * - Deposit preauthorization rules apply for the destination.
 *
 * @note Funds are deposited into the destination's inbox, not spending
 *       balance. The recipient must call ConfidentialMPTMergeInbox to make
 *       received funds spendable.
 *
 * @see ConfidentialMPTMergeInbox, ConfidentialMPTConvert,
 * ConfidentialMPTConvertBack
 */
class ConfidentialMPTSend : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit ConfidentialMPTSend(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    /**
     * @brief Validates transaction fields before any ledger access.
     *
     * Checks:
     * - Feature `featureConfidentialTransfer` is enabled.
     * - Sender is not the MPT issuer.
     * - Sender is not sending to themselves.
     * - All encrypted amount fields are correct length (128 bytes each).
     * - ZKProof length matches expected size for equality + linkage proofs.
     * - Pedersen commitment lengths are correct (64 bytes each).
     * - All ciphertexts have valid format (parseable as curve points).
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
     * - Sender and destination accounts exist.
     * - MPTokenIssuance exists with transfer and privacy enabled.
     * - Issuer has registered ElGamal public key.
     * - Auditor requirements match.
     * - Both sender and destination MPTokens exist with required fields.
     * - Neither account is frozen or unauthorized.
     * - Verifies all zero-knowledge proofs:
     *   - Multi-ciphertext equality proof
     *   - Amount Pedersen linkage proof
     *   - Balance Pedersen linkage proof
     *
     * @param ctx The preclaim context containing view and transaction.
     * @return tesSUCCESS if valid, or an appropriate error code.
     */
    static TER
    preclaim(PreclaimContext const& ctx);

    /**
     * @brief Applies the confidential transfer to the ledger.
     *
     * - Verifies deposit preauthorization for destination.
     * - Subtracts encrypted amounts from sender's balances.
     * - Adds encrypted amounts to destination's balances.
     * - Increments version counters for both accounts.
     *
     * @return tesSUCCESS on success, or an appropriate error code.
     */
    TER
    doApply() override;
};

}  // namespace xrpl

#endif
