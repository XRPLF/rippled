#ifndef XRPL_PROTOCOL_CONFIDENTIALTRANSFER_H_INCLUDED
#define XRPL_PROTOCOL_CONFIDENTIALTRANSFER_H_INCLUDED

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

#include <secp256k1.h>

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
    Slice const publicKey;        ///< The recipient's ElGamal public key (64 bytes).
    Slice const encryptedAmount;  ///< The encrypted amount ciphertext (128 bytes).
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
    mptoken[sfConfidentialBalanceVersion] = mptoken[~sfConfidentialBalanceVersion].value_or(0u) + 1u;
}

/**
 * @brief Adds common fields to a serializer for ZKP context hash generation.
 *
 * Serializes the transaction type, account, sequence number, and issuance ID
 * into the provided serializer. These fields form the base of all context
 * hashes used in zero-knowledge proofs.
 *
 * @param s          The serializer to append fields to.
 * @param txType     The transaction type identifier.
 * @param account    The account ID of the transaction sender.
 * @param sequence   The transaction sequence number.
 * @param issuanceID The MPToken Issuance ID.
 */
void
addCommonZKPFields(
    Serializer& s,
    std::uint16_t txType,
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID);

/**
 * @brief Generates the context hash for ConfidentialMPTSend transactions.
 *
 * Creates a unique 256-bit hash that binds the zero-knowledge proofs to
 * this specific send transaction, preventing proof reuse across transactions.
 *
 * @param account     The sender's account ID.
 * @param sequence    The transaction sequence number.
 * @param issuanceID  The MPToken Issuance ID.
 * @param destination The destination account ID.
 * @param version     The sender's confidential balance version.
 * @return A 256-bit context hash unique to this transaction.
 */
uint256
getSendContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    AccountID const& destination,
    std::uint32_t version);

/**
 * @brief Generates the context hash for ConfidentialMPTClawback transactions.
 *
 * Creates a unique 256-bit hash that binds the equality proof to this
 * specific clawback transaction.
 *
 * @param account    The issuer's account ID.
 * @param sequence   The transaction sequence number.
 * @param issuanceID The MPToken Issuance ID.
 * @param amount     The amount being clawed back.
 * @param holder     The holder's account ID being clawed back from.
 * @return A 256-bit context hash unique to this transaction.
 */
uint256
getClawbackContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    std::uint64_t amount,
    AccountID const& holder);

/**
 * @brief Generates the context hash for ConfidentialMPTConvert transactions.
 *
 * Creates a unique 256-bit hash that binds the Schnorr proof (for key
 * registration) to this specific convert transaction.
 *
 * @param account    The holder's account ID.
 * @param sequence   The transaction sequence number.
 * @param issuanceID The MPToken Issuance ID.
 * @param amount     The amount being converted to confidential.
 * @return A 256-bit context hash unique to this transaction.
 */
uint256
getConvertContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    std::uint64_t amount);

/**
 * @brief Generates the context hash for ConfidentialMPTConvertBack transactions.
 *
 * Creates a unique 256-bit hash that binds the zero-knowledge proofs to
 * this specific convert-back transaction.
 *
 * @param account    The holder's account ID.
 * @param sequence   The transaction sequence number.
 * @param issuanceID The MPToken Issuance ID.
 * @param amount     The amount being converted back to public.
 * @param version    The holder's confidential balance version.
 * @return A 256-bit context hash unique to this transaction.
 */
uint256
getConvertBackContextHash(
    AccountID const& account,
    std::uint32_t sequence,
    uint192 const& issuanceID,
    std::uint64_t amount,
    std::uint32_t version);

/**
 * @brief Parses an ElGamal ciphertext into two secp256k1 public key components.
 *
 * Breaks a 66-byte encrypted amount (two 33-byte compressed EC points) into
 * two secp256k1_pubkey structures (C1, C2) for use in cryptographic operations.
 *
 * @param buffer The 66-byte buffer containing the compressed ciphertext.
 * @param out1   Output: The C1 component of the ElGamal ciphertext.
 * @param out2   Output: The C2 component of the ElGamal ciphertext.
 * @return true if parsing succeeds, false if the buffer is invalid.
 */
bool
makeEcPair(Slice const& buffer, secp256k1_pubkey& out1, secp256k1_pubkey& out2);

/**
 * @brief Serializes two secp256k1 public key components into compressed form.
 *
 * Converts two secp256k1_pubkey structures (C1, C2) back into a 66-byte
 * buffer containing two 33-byte compressed EC points.
 *
 * @param in1    The C1 component to serialize.
 * @param in2    The C2 component to serialize.
 * @param buffer Output: The 66-byte buffer to write the compressed ciphertext.
 * @return true if serialization succeeds, false otherwise.
 */
bool
serializeEcPair(secp256k1_pubkey const& in1, secp256k1_pubkey const& in2, Buffer& buffer);

/**
 * @brief Verifies that a buffer contains two valid, parsable EC public keys.
 *
 * @param buffer The input buffer containing two concatenated components.
 * @return true if both components can be parsed successfully, false otherwise.
 */
bool
isValidCiphertext(Slice const& buffer);

/**
 * @brief Homomorphically adds two ElGamal ciphertexts.
 *
 * Uses the additive homomorphic property of ElGamal encryption to compute
 * Enc(a + b) from Enc(a) and Enc(b) without decryption.
 *
 * @param a   The first ciphertext (66 bytes).
 * @param b   The second ciphertext (66 bytes).
 * @param out Output: The resulting ciphertext Enc(a + b).
 * @return tesSUCCESS on success, or an error code if parsing fails.
 */
TER
homomorphicAdd(Slice const& a, Slice const& b, Buffer& out);

/**
 * @brief Homomorphically subtracts two ElGamal ciphertexts.
 *
 * Uses the additive homomorphic property of ElGamal encryption to compute
 * Enc(a - b) from Enc(a) and Enc(b) without decryption.
 *
 * @param a   The minuend ciphertext (66 bytes).
 * @param b   The subtrahend ciphertext (66 bytes).
 * @param out Output: The resulting ciphertext Enc(a - b).
 * @return tesSUCCESS on success, or an error code if parsing fails.
 */
TER
homomorphicSubtract(Slice const& a, Slice const& b, Buffer& out);

/**
 * @brief Encrypts an amount using ElGamal encryption.
 *
 * Produces a ciphertext C = (C1, C2) where C1 = r*G and C2 = m*G + r*Pk,
 * using the provided blinding factor r.
 *
 * @param amt            The plaintext amount to encrypt.
 * @param pubKeySlice    The recipient's ElGamal public key (64 bytes).
 * @param blindingFactor The 32-byte randomness used as blinding factor r.
 * @return The 66-byte ciphertext, or std::nullopt on failure.
 */
std::optional<Buffer>
encryptAmount(uint64_t const amt, Slice const& pubKeySlice, Slice const& blindingFactor);

/**
 * @brief Generates the canonical zero encryption for a specific MPToken.
 *
 * Creates a deterministic encryption of zero that is unique to the account
 * and MPT issuance. Used to initialize confidential balance fields.
 *
 * @param pubKeySlice The holder's ElGamal public key (64 bytes).
 * @param account     The account ID of the token holder.
 * @param mptId       The MPToken Issuance ID.
 * @return The 66-byte canonical zero ciphertext, or std::nullopt on failure.
 */
std::optional<Buffer>
encryptCanonicalZeroAmount(Slice const& pubKeySlice, AccountID const& account, MPTID const& mptId);

/**
 * @brief Verifies a Schnorr proof of knowledge of an ElGamal private key.
 *
 * Proves that the submitter knows the secret key corresponding to the
 * provided public key, without revealing the secret key itself.
 *
 * @param pubKeySlice The ElGamal public key (64 bytes).
 * @param proofSlice  The Schnorr proof (65 bytes).
 * @param contextHash The 256-bit context hash binding the proof.
 * @return tesSUCCESS if valid, or an error code otherwise.
 */
TER
verifySchnorrProof(Slice const& pubKeySlice, Slice const& proofSlice, uint256 const& contextHash);

/**
 * @brief Verifies that a ciphertext correctly encrypts a revealed amount.
 *
 * Given the plaintext amount and blinding factor, verifies that the
 * ciphertext was correctly constructed using ElGamal encryption.
 *
 * @param amount         The revealed plaintext amount.
 * @param blindingFactor The 32-byte blinding factor used in encryption.
 * @param pubKeySlice    The recipient's ElGamal public key (64 bytes).
 * @param ciphertext     The ciphertext to verify (66 bytes).
 * @return tesSUCCESS if the encryption is valid, or an error code otherwise.
 */
TER
verifyElGamalEncryption(
    std::uint64_t const amount,
    Slice const& blindingFactor,
    Slice const& pubKeySlice,
    Slice const& ciphertext);

/**
 * @brief Validates the format of encrypted amount fields in a transaction.
 *
 * Checks that all ciphertext fields in the transaction object have the
 * correct length and contain valid EC points.
 *
 * @param object The transaction object containing encrypted amount fields.
 * @return tesSUCCESS if all formats are valid, or an error code otherwise.
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
 * @param blindingFactor The 32-byte blinding factor used in all encryptions.
 * @param holder         The holder's public key and encrypted amount.
 * @param issuer         The issuer's public key and encrypted amount.
 * @param auditor        Optional auditor's public key and encrypted amount.
 * @return tesSUCCESS if all encryptions are valid, or an error code otherwise.
 */
TER
verifyRevealedAmount(
    std::uint64_t const amount,
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
 * @brief Calculates the size of a multi-ciphertext equality proof.
 *
 * The proof size varies based on the number of recipients because each
 * additional recipient requires additional proof components.
 *
 * @param nRecipients The number of recipients in the transfer.
 * @return The size in bytes of the equality proof.
 */
std::size_t
getMultiCiphertextEqualityProofSize(std::size_t nRecipients);

/**
 * @brief Verifies a multi-ciphertext equality proof.
 *
 * Proves that all ciphertexts in the recipients vector encrypt the same
 * plaintext amount, without revealing the amount itself.
 *
 * @param proof       The zero-knowledge proof bytes.
 * @param recipients  Vector of recipients with their public keys and ciphertexts.
 * @param nRecipients The number of recipients (must match recipients.size()).
 * @param contextHash The 256-bit context hash binding the proof.
 * @return tesSUCCESS if the proof is valid, or an error code otherwise.
 */
TER
verifyMultiCiphertextEqualityProof(
    Slice const& proof,
    std::vector<ConfidentialRecipient> const& recipients,
    std::size_t const nRecipients,
    uint256 const& contextHash);

/**
 * @brief Verifies a clawback equality proof.
 *
 * Proves that the issuer knows the exact amount encrypted in the holder's
 * balance ciphertext. Used in ConfidentialMPTClawback to verify the issuer
 * can decrypt the balance using their private key.
 *
 * @param amount      The revealed plaintext amount.
 * @param proof       The zero-knowledge proof bytes.
 * @param pubKeySlice The issuer's ElGamal public key (64 bytes).
 * @param ciphertext  The issuer's encrypted balance on the holder's account (66 bytes).
 * @param contextHash The 256-bit context hash binding the proof.
 * @return tesSUCCESS if the proof is valid, or an error code otherwise.
 */
TER
verifyClawbackEqualityProof(
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
 * @brief Verifies the cryptographic link between an ElGamal Ciphertext and a
 * Pedersen Commitment for a transaction Amount.
 *
 * It proves that the ElGamal ciphertext `encAmt` encrypts the same value `m`
 * as the Pedersen Commitment `pcmSlice`, using the randomness `r`.
 * Proves Enc(m) <-> Pcm(m)
 *
 * @param proof       The Zero Knowledge Proof bytes.
 * @param encAmt      The ElGamal ciphertext of the amount (C1, C2).
 * @param pubKeySlice The sender's public key.
 * @param pcmSlice    The Pedersen Commitment to the amount.
 * @param contextHash The unique context hash for this transaction.
 * @return tesSUCCESS if the proof is valid, or an error code otherwise.
 */
TER
verifyAmountPcmLinkage(
    Slice const& proof,
    Slice const& encAmt,
    Slice const& pubKeySlice,
    Slice const& pcmSlice,
    uint256 const& contextHash);

/**
 * @brief Verifies the cryptographic link between an ElGamal Ciphertext and a
 * Pedersen Commitment for an account Balance.
 *
 * It proves that the ElGamal ciphertext `encAmt` encrypts the same value `b`
 * as the Pedersen Commitment `pcmSlice`, using the secret key `s`.
 * Proves Enc(b) <-> Pcm(b)
 *
 * Note: Swaps arguments (Pk <-> C1) to accommodate the different algebraic
 * structure.
 *
 * @param proof       The Zero Knowledge Proof bytes.
 * @param encAmt      The ElGamal ciphertext of the balance (C1, C2).
 * @param pubKeySlice The sender's public key.
 * @param pcmSlice    The Pedersen Commitment to the balance.
 * @param contextHash The unique context hash for this transaction.
 * @return tesSUCCESS if the proof is valid, or an error code otherwise.
 */
TER
verifyBalancePcmLinkage(
    Slice const& proof,
    Slice const& encAmt,
    Slice const& pubKeySlice,
    Slice const& pcmSlice,
    uint256 const& contextHash);

// The following functions belong to the mpt-crypto library,
// they will be finally removed and we will use conan2 to manage the dependency.
/**
 * @brief Generates a new secp256k1 key pair.
 */
SECP256K1_API int
secp256k1_elgamal_generate_keypair(secp256k1_context const* ctx, unsigned char* privkey, secp256k1_pubkey* pubkey);

/**
 * @brief Encrypts a 64-bit amount using ElGamal.
 */
SECP256K1_API int
secp256k1_elgamal_encrypt(
    secp256k1_context const* ctx,
    secp256k1_pubkey* c1,
    secp256k1_pubkey* c2,
    secp256k1_pubkey const* pubkey_Q,
    uint64_t amount,
    unsigned char const* blinding_factor);

/**
 * @brief Decrypts an ElGamal ciphertext to recover the amount.
 */
SECP256K1_API int
secp256k1_elgamal_decrypt(
    secp256k1_context const* ctx,
    uint64_t* amount,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    unsigned char const* privkey);

/**
 * @brief Homomorphically adds two ElGamal ciphertexts.
 */
SECP256K1_API int
secp256k1_elgamal_add(
    secp256k1_context const* ctx,
    secp256k1_pubkey* sum_c1,
    secp256k1_pubkey* sum_c2,
    secp256k1_pubkey const* a_c1,
    secp256k1_pubkey const* a_c2,
    secp256k1_pubkey const* b_c1,
    secp256k1_pubkey const* b_c2);

/**
 * @brief Homomorphically subtracts two ElGamal ciphertexts.
 */
SECP256K1_API int
secp256k1_elgamal_subtract(
    secp256k1_context const* ctx,
    secp256k1_pubkey* diff_c1,
    secp256k1_pubkey* diff_c2,
    secp256k1_pubkey const* a_c1,
    secp256k1_pubkey const* a_c2,
    secp256k1_pubkey const* b_c1,
    secp256k1_pubkey const* b_c2);

/**
 * @brief Generates the canonical encrypted zero for a given MPT token instance.
 *
 * This ciphertext represents a zero balance for a specific account's holding
 * of a token defined by its MPTokenIssuanceID.
 *
 * @param[in]   ctx             A pointer to a valid secp256k1 context.
 * @param[out]  enc_zero_c1     The C1 component of the canonical ciphertext.
 * @param[out]  enc_zero_c2     The C2 component of the canonical ciphertext.
 * @param[in]   pubkey          The ElGamal public key of the account holder.
 * @param[in]   account_id      A pointer to the 20-byte AccountID.
 * @param[in]   mpt_issuance_id A pointer to the 24-byte MPTokenIssuanceID.
 *
 * @return 1 on success, 0 on failure.
 */
SECP256K1_API int
generate_canonical_encrypted_zero(
    secp256k1_context const* ctx,
    secp256k1_pubkey* enc_zero_c1,
    secp256k1_pubkey* enc_zero_c2,
    secp256k1_pubkey const* pubkey,
    unsigned char const* account_id,      // 20 bytes
    unsigned char const* mpt_issuance_id  // 24 bytes
);

/**
 * Generates a cryptographically secure 32-byte scalar (private key).
 * @return 1 on success, 0 on failure.
 */
SECP256K1_API int
generate_random_scalar(secp256k1_context const* ctx, unsigned char* scalar_bytes);

/**
 * Computes the point M = amount * G.
 * IMPORTANT: This function MUST NOT be called with amount = 0.
 */
SECP256K1_API int
compute_amount_point(secp256k1_context const* ctx, secp256k1_pubkey* mG, uint64_t amount);

/**
 * Builds the challenge hash input for the NON-ZERO amount case.
 * Output buffer must be 253 bytes.
 */
SECP256K1_API void
build_challenge_hash_input_nonzero(
    unsigned char* hash_input,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* mG,
    secp256k1_pubkey const* T1,
    secp256k1_pubkey const* T2,
    unsigned char const* tx_context_id);

/**
 * Builds the challenge hash input for the ZERO amount case.
 * Output buffer must be 220 bytes.
 */
SECP256K1_API void
build_challenge_hash_input_zero(
    unsigned char* hash_input,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* T1,
    secp256k1_pubkey const* T2,
    unsigned char const* tx_context_id);

/**
 * @brief Proves that a commitment (C1, C2) encrypts a specific plaintext
 * 'amount'.
 */
SECP256K1_API int
secp256k1_equality_plaintext_prove(
    secp256k1_context const* ctx,
    unsigned char* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk_recipient,
    uint64_t amount,
    unsigned char const* randomness_r,
    unsigned char const* tx_context_id);

/**
 * @brief Verifies the proof generated by secp256k1_equality_plaintext_prove.
 */
SECP256K1_API int
secp256k1_equality_plaintext_verify(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk_recipient,
    uint64_t amount,
    unsigned char const* tx_context_id);

void
build_pok_challenge(
    unsigned char* e,
    secp256k1_context const* ctx,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* T,
    unsigned char const* context_id);

/** Proof of Knowledge of Secret Key for Registration */
int
secp256k1_mpt_pok_sk_prove(
    secp256k1_context const* ctx,
    unsigned char* proof, /* Expected size: 65 bytes */
    secp256k1_pubkey const* pk,
    unsigned char const* sk,
    unsigned char const* context_id);

int
secp256k1_mpt_pok_sk_verify(
    secp256k1_context const* ctx,
    unsigned char const* proof, /* Expected size: 65 bytes */
    secp256k1_pubkey const* pk,
    unsigned char const* context_id);

/**
 * Verifies that (c1, c2) is a valid ElGamal encryption of 'amount'
 * for 'pubkey_Q' using the revealed 'blinding_factor'.
 */
int
secp256k1_elgamal_verify_encryption(
    secp256k1_context const* ctx,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pubkey_Q,
    uint64_t amount,
    unsigned char const* blinding_factor);

/**
 * @brief Proves the link between an ElGamal ciphertext and a Pedersen
 * commitment.
 * * Formal Statement: Knowledge of (m, r, rho) such that:
 * C1 = r*G, C2 = m*G + r*Pk, and PCm = m*G + rho*H.
 * * @param ctx         Pointer to a secp256k1 context object.
 * @param proof       [OUT] Pointer to 195-byte buffer for the proof output.
 * @param c1          Pointer to the ElGamal C1 point (r*G).
 * @param c2          Pointer to the ElGamal C2 point (m*G + r*Pk).
 * @param pk          Pointer to the recipient's public key.
 * @param pcm         Pointer to the Pedersen Commitment (m*G + rho*H).
 * @param amount      The plaintext amount (m).
 * @param r           The 32-byte secret ElGamal blinding factor.
 * @param rho         The 32-byte secret Pedersen blinding factor.
 * @param context_id  32-byte unique transaction context identifier.
 * @return 1 on success, 0 on failure.
 */
int
secp256k1_elgamal_pedersen_link_prove(
    secp256k1_context const* ctx,
    unsigned char* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* pcm,
    uint64_t amount,
    unsigned char const* r,
    unsigned char const* rho,
    unsigned char const* context_id);

/**
 * @brief Verifies the link proof between ElGamal and Pedersen commitments.
 * * @return 1 if the proof is valid, 0 otherwise.
 */
int
secp256k1_elgamal_pedersen_link_verify(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    secp256k1_pubkey const* c1,
    secp256k1_pubkey const* c2,
    secp256k1_pubkey const* pk,
    secp256k1_pubkey const* pcm,
    unsigned char const* context_id);

/**
 * Compute a Pedersen Commitment: PC = m*G + rho*H
 * Returns 1 on success, 0 on failure.
 */
int
secp256k1_mpt_pedersen_commit(
    secp256k1_context const* ctx,
    secp256k1_pubkey* commitment,
    uint64_t amount,
    unsigned char const* blinding_factor_rho /* 32 bytes */
);

// Multi-proof for same plaintexts
void
build_hash_input(
    unsigned char* hash_out,  // Output: 32-byte hash
    size_t n,
    secp256k1_pubkey const* R,
    secp256k1_pubkey const* S,
    secp256k1_pubkey const* Pk,
    secp256k1_pubkey const* T_m,
    secp256k1_pubkey const* T_rG,
    secp256k1_pubkey const* T_rP,
    unsigned char const* tx_id);

size_t
secp256k1_mpt_prove_same_plaintext_multi_size(size_t n);

int
secp256k1_mpt_prove_same_plaintext_multi(
    secp256k1_context const* ctx,
    unsigned char* proof_out,
    size_t* proof_len,
    uint64_t amount_m,
    size_t n,
    secp256k1_pubkey const* R,
    secp256k1_pubkey const* S,
    secp256k1_pubkey const* Pk,
    unsigned char const* r_array,
    unsigned char const* tx_id);

int
secp256k1_mpt_verify_same_plaintext_multi(
    secp256k1_context const* ctx,
    unsigned char const* proof,
    size_t proof_len,
    size_t n,
    secp256k1_pubkey const* R,
    secp256k1_pubkey const* S,
    secp256k1_pubkey const* Pk,
    unsigned char const* tx_id);
}  // namespace xrpl

#endif
