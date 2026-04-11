#pragma once

#include <xrpl/basics/Slice.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rate.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/detail/secp256k1.h>

#include <secp256k1_mpt.h>

namespace xrpl {

/**
 * @brief Bundles an ElGamal public key with its associated encrypted amount.
 *
 * Used to represent a recipient in confidential transfers, containing both
 * the recipient's ElGamal public key and the ciphertext encrypting the
 * transfer amount under that key.
 */
struct ConfidentialRecipient
{
    Slice publicKey;        ///< The recipient's ElGamal public key (size=xrpl::ecPubKeyLength).
    Slice encryptedAmount;  ///< The encrypted amount ciphertext
                            ///< (size=xrpl::ecGamalEncryptedTotalLength).
};

/// Holds two secp256k1 public key components representing an ElGamal ciphertext (C1, C2).
struct EcPair
{
    secp256k1_pubkey c1;
    secp256k1_pubkey c2;
};

/**
 * @brief Increments the confidential balance version counter on an MPToken.
 *
 * The version counter is used to prevent replay attacks by binding proofs
 * to a specific state of the account's confidential balance. Wraps to 0
 * on overflow (defined behavior for unsigned integers).
 *
 * @param mptoken The MPToken ledger entry to update.
 */
inline void
incrementConfidentialVersion(STObject& mptoken)
{
    // Retrieve current version and increment.
    // Unsigned integer overflow is defined behavior in C++ (wraps to 0),
    // which is acceptable here.
    mptoken[sfConfidentialBalanceVersion] =
        mptoken[~sfConfidentialBalanceVersion].value_or(0u) + 1u;
}

/**
 * @brief Generates the context hash for ConfidentialMPTSend transactions.
 *
 * Creates a unique 256-bit hash that binds the zero-knowledge proofs to
 * this specific send transaction, preventing proof reuse across transactions.
 *
 * @param account     The sender's account ID.
 * @param issuanceID  The MPToken Issuance ID.
 * @param sequence    The transaction sequence number or ticket number.
 * @param destination The destination account ID.
 * @param version     The sender's confidential balance version.
 * @return A 256-bit context hash unique to this transaction.
 */
uint256
getSendContextHash(
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence,
    AccountID const& destination,
    std::uint32_t version);

/**
 * @brief Generates the context hash for ConfidentialMPTClawback transactions.
 *
 * Creates a unique 256-bit hash that binds the equality proof to this
 * specific clawback transaction.
 *
 * @param account    The issuer's account ID.
 * @param issuanceID The MPToken Issuance ID.
 * @param sequence   The transaction sequence number or ticket number.
 * @param holder     The holder's account ID being clawed back from.
 * @return A 256-bit context hash unique to this transaction.
 */
uint256
getClawbackContextHash(
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence,
    AccountID const& holder);

/**
 * @brief Generates the context hash for ConfidentialMPTConvert transactions.
 *
 * Creates a unique 256-bit hash that binds the Schnorr proof (for key
 * registration) to this specific convert transaction.
 *
 * @param account    The holder's account ID.
 * @param issuanceID The MPToken Issuance ID.
 * @param sequence   The transaction sequence number or a ticket number.
 * @return A 256-bit context hash unique to this transaction.
 */
uint256
getConvertContextHash(AccountID const& account, uint192 const& issuanceID, std::uint32_t sequence);

/**
 * @brief Generates the context hash for ConfidentialMPTConvertBack transactions.
 *
 * Creates a unique 256-bit hash that binds the zero-knowledge proofs to
 * this specific convert-back transaction.
 *
 * @param account    The holder's account ID.
 * @param issuanceID The MPToken Issuance ID.
 * @param sequence   The transaction sequence number or a ticket number.
 * @param version    The holder's confidential balance version.
 * @return A 256-bit context hash unique to this transaction.
 */
uint256
getConvertBackContextHash(
    AccountID const& account,
    uint192 const& issuanceID,
    std::uint32_t sequence,
    std::uint32_t version);

/**
 * @brief Parses an ElGamal ciphertext into two secp256k1 public key components.
 *
 * Breaks a 66-byte encrypted amount (two 33-byte compressed EC points) into
 * a pair containing (C1, C2) for use in cryptographic operations.
 *
 * @param buffer The 66-byte buffer containing the compressed ciphertext.
 * @return The parsed pair (c1, c2) if successful, std::nullopt if the buffer is invalid.
 */
std::optional<EcPair>
makeEcPair(Slice const& buffer);

/**
 * @brief Serializes an EcPair into compressed form.
 *
 * Converts an EcPair (C1, C2) back into a 66-byte buffer containing
 * two 33-byte compressed EC points.
 *
 * @param pair The EcPair to serialize.
 * @return The 66-byte buffer, or std::nullopt if serialization fails.
 */
std::optional<Buffer>
serializeEcPair(EcPair const& pair);

/**
 * @brief Verifies that a buffer contains two valid, parsable EC public keys.
 *
 * @param buffer The input buffer containing two concatenated components.
 * @return true if both components can be parsed successfully, false otherwise.
 */
bool
isValidCiphertext(Slice const& buffer);

/**
 * @brief Verifies that a buffer contains a valid, parsable compressed EC point.
 *
 * Can be used to validate both compressed public keys and Pedersen commitments.
 * Fails early if the prefix byte is not 0x02 or 0x03.
 *
 * @param buffer The input buffer containing a compressed EC point (33 bytes).
 * @return true if the point can be parsed successfully, false otherwise.
 */
bool
isValidCompressedECPoint(Slice const& buffer);

/**
 * @brief Homomorphically adds two ElGamal ciphertexts.
 *
 * Uses the additive homomorphic property of ElGamal encryption to compute
 * Enc(a + b) from Enc(a) and Enc(b) without decryption.
 *
 * @param a The first ciphertext (66 bytes).
 * @param b The second ciphertext (66 bytes).
 * @return The resulting ciphertext Enc(a + b), or std::nullopt on failure.
 */
std::optional<Buffer>
homomorphicAdd(Slice const& a, Slice const& b);

/**
 * @brief Homomorphically subtracts two ElGamal ciphertexts.
 *
 * Uses the additive homomorphic property of ElGamal encryption to compute
 * Enc(a - b) from Enc(a) and Enc(b) without decryption.
 *
 * @param a The minuend ciphertext (66 bytes).
 * @param b The subtrahend ciphertext (66 bytes).
 * @return The resulting ciphertext Enc(a - b), or std::nullopt on failure.
 */
std::optional<Buffer>
homomorphicSubtract(Slice const& a, Slice const& b);

/**
 * @brief Encrypts an amount using ElGamal encryption.
 *
 * Produces a ciphertext C = (C1, C2) where C1 = r*G and C2 = m*G + r*Pk,
 * using the provided blinding factor r.
 *
 * @param amt            The plaintext amount to encrypt.
 * @param pubKeySlice    The recipient's ElGamal public key (size=xrpl::ecPubKeyLength).
 * @param blindingFactor The randomness used as blinding factor r
 * (size=xrpl::ecBlindingFactorLength).
 * @return The ciphertext (size=xrpl::ecGamalEncryptedTotalLength), or std::nullopt on failure.
 */
std::optional<Buffer>
encryptAmount(uint64_t const amt, Slice const& pubKeySlice, Slice const& blindingFactor);

/**
 * @brief Generates the canonical zero encryption for a specific MPToken.
 *
 * Creates a deterministic encryption of zero that is unique to the account
 * and MPT issuance. Used to initialize confidential balance fields.
 *
 * @param pubKeySlice The holder's ElGamal public key (size=xrpl::ecPubKeyLength).
 * @param account     The account ID of the token holder.
 * @param mptId       The MPToken Issuance ID.
 * @return The canonical zero ciphertext (size=xrpl::ecGamalEncryptedTotalLength), or std::nullopt
 * on failure.
 */
std::optional<Buffer>
encryptCanonicalZeroAmount(Slice const& pubKeySlice, AccountID const& account, MPTID const& mptId);

/**
 * @brief Verifies a Schnorr proof of knowledge of an ElGamal private key.
 *
 * Proves that the submitter knows the secret key corresponding to the
 * provided public key, without revealing the secret key itself.
 *
 * @param pubKeySlice The ElGamal public key (size=xrpl::ecPubKeyLength).
 * @param proofSlice  The Schnorr proof (size=xrpl::ecSchnorrProofLength).
 * @param contextHash The 256-bit context hash binding the proof.
 * @return tesSUCCESS if valid, or an error code otherwise.
 */
TER
verifySchnorrProof(Slice const& pubKeySlice, Slice const& proofSlice, uint256 const& contextHash);

/**
 * @brief Validates the format of encrypted amount fields in a transaction.
 *
 * Checks that all ciphertext fields in the transaction object have the
 * correct length and contain valid EC points. This function is only used
 * by ConfidentialMPTConvert and ConfidentialMPTConvertBack transactions.
 *
 * @param object The transaction object containing encrypted amount fields.
 * @return tesSUCCESS if all formats are valid, temMALFORMED if required fields
 *         are missing, or temBAD_CIPHERTEXT if format validation fails.
 */
NotTEC
checkEncryptedAmountFormat(STObject const& object);

/**
 * @brief Verifies revealed amount encryptions for all recipients.
 *
 * Validates that the same amount was correctly encrypted for the holder,
 * issuer, and optionally the auditor using their respective public keys.
 *
 * @param amount         The revealed plaintext amount.
 * @param blindingFactor The blinding factor used in all encryptions
 * (size=xrpl::ecBlindingFactorLength).
 * @param holder         The holder's public key and encrypted amount.
 * @param issuer         The issuer's public key and encrypted amount.
 * @param auditor        Optional auditor's public key and encrypted amount.
 * @return tesSUCCESS if all encryptions are valid, or an error code otherwise.
 */
TER
verifyRevealedAmount(
    uint64_t const amount,
    Slice const& blindingFactor,
    ConfidentialRecipient const& holder,
    ConfidentialRecipient const& issuer,
    std::optional<ConfidentialRecipient> const& auditor);

/**
 * @brief Returns the number of recipients in a confidential transfer.
 *
 * Returns 4 if an auditor is present (sender, destination, issuer, auditor),
 * or 3 if no auditor (sender, destination, issuer).
 *
 * @param hasAuditor Whether the issuance has an auditor configured.
 * @return The number of recipients (3 or 4).
 */
constexpr std::size_t
getConfidentialRecipientCount(bool hasAuditor)
{
    return hasAuditor ? 4 : 3;
}

/**
 * @brief Verifies a compact sigma clawback proof.
 *
 * Proves that the issuer knows the exact amount encrypted in the holder's
 * balance ciphertext. Used in ConfidentialMPTClawback to verify the issuer
 * can decrypt the balance using their private key.
 *
 * @param amount      The revealed plaintext amount.
 * @param proof       The zero-knowledge proof bytes (ecCompactClawbackProofLength).
 * @param pubKeySlice The issuer's ElGamal public key (ecPubKeyLength bytes).
 * @param ciphertext  The issuer's encrypted balance on the holder's account
 * (ecGamalEncryptedTotalLength bytes).
 * @param contextHash The 256-bit context hash binding the proof.
 * @return tesSUCCESS if the proof is valid, or an error code otherwise.
 */
TER
verifyClawbackProof(
    uint64_t const amount,
    Slice const& proof,
    Slice const& pubKeySlice,
    Slice const& ciphertext,
    uint256 const& contextHash);

/**
 * @brief Generates a cryptographically secure 32-byte blinding factor.
 *
 * Produces random bytes suitable for use as an ElGamal blinding factor
 * or Pedersen commitment randomness.
 *
 * @return A 32-byte buffer containing the random blinding factor.
 */
Buffer
generateBlindingFactor();

/**
 * @brief Verifies all zero-knowledge proofs for a ConfidentialMPTSend transaction.
 *
 * This function calls mpt_verify_send_proof API in the mpt-crypto utility lib, which verifies the
 * equality proof, amount linkage, balance linkage, and range proof.
 * Equality proof: Proves the same value is encrypted for the sender, receiver, issuer, and auditor.
 * Amount linkage: Proves the send amount matches the amount Pedersen commitment.
 * Balance linkage: Proves the sender's balance matches the balance Pedersen
 * commitment.
 * Range proof: Proves the amount and the remaining balance are within range [0, 2^64-1].
 *
 * @param proof             The full proof blob.
 * @param sender            The sender's public key and encrypted amount.
 * @param destination       The destination's public key and encrypted amount.
 * @param issuer            The issuer's public key and encrypted amount.
 * @param auditor           The auditor's public key and encrypted amount if present.
 * @param spendingBalance   The sender's current spending balance ciphertext.
 * @param amountCommitment  The Pedersen commitment to the send amount.
 * @param balanceCommitment The Pedersen commitment to the sender's balance.
 * @param contextHash       The context hash binding the proof.
 * @return tesSUCCESS if all proofs are valid, or an error code otherwise.
 */
TER
verifySendProof(
    Slice const& proof,
    ConfidentialRecipient const& sender,
    ConfidentialRecipient const& destination,
    ConfidentialRecipient const& issuer,
    std::optional<ConfidentialRecipient> const& auditor,
    Slice const& spendingBalance,
    Slice const& amountCommitment,
    Slice const& balanceCommitment,
    uint256 const& contextHash);

/**
 * @brief Verifies all zero-knowledge proofs for a ConfidentialMPTConvertBack transaction.
 *
 * This function calls mpt_verify_convert_back_proof API in the mpt-crypto utility lib, which
 * verifies the balance linkage proof and range proof. Balance linkage proof: proves the balance
 * commitment matches the spending ciphertext. Range proof: proves the remaining balance after
 * convert back is within range [0, 2^64-1].
 *
 * @param proof             The full proof blob.
 * @param pubKeySlice       The holder's public key.
 * @param spendingBalance   The holder's spending balance ciphertext.
 * @param balanceCommitment The Pedersen commitment to the balance.
 * @param amount            The amount being converted back to public.
 * @param contextHash       The context hash binding the proof.
 * @return tesSUCCESS if all proofs are valid, or an error code otherwise.
 */
TER
verifyConvertBackProof(
    Slice const& proof,
    Slice const& pubKeySlice,
    Slice const& spendingBalance,
    Slice const& balanceCommitment,
    uint64_t amount,
    uint256 const& contextHash);

}  // namespace xrpl
