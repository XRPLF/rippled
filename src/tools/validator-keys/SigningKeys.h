#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/server/Manifest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace boost {
namespace filesystem {
class path;
}
}  // namespace boost

namespace xrpl {

/**
 * Returns the token as the base64 JSON object written to [validator_token].
 */
std::string
tokenToBase64(ValidatorToken const& token);

/**
 * The master key of a validator or a validator-list publisher, as stored in
 * the key file, with the manifest, token and revocation operations that the
 * master key signs.
 *
 * The secret key is optional. When it is absent the key file was created with
 * `create_external` and every master signature comes from an external signer:
 * the `start*` methods return the bytes to sign as hex and the `finish*`
 * methods take the signature back.
 */
class SigningKeys
{
private:
    struct Keys
    {
        PublicKey publicKey;
        // Unset when the master key is held by an external signer.
        std::optional<SecretKey> secretKey;

        Keys() = delete;
        Keys(std::pair<PublicKey, SecretKey> const& p) : publicKey(p.first), secretKey(p.second)
        {
        }
        Keys(PublicKey const& pub) : publicKey(pub), secretKey(std::nullopt)
        {
        }
    };

    KeyType const keyType_;
    Keys const keys_;
    std::vector<std::uint8_t> manifest_;
    std::uint32_t tokenSequence_;
    bool revoked_;
    std::string domain_;
    // A token started with `startValidatorToken` and not yet finished. Only
    // one of the two is set: the software signing key generated for the
    // token, or the external signing key the token will delegate to.
    mutable std::optional<SecretKey> pendingTokenSecret_;
    mutable std::optional<PublicKey> pendingSigningKey_;
    mutable std::optional<KeyType> pendingKeyType_;

public:
    explicit SigningKeys(KeyType const& keyType);

    SigningKeys(
        KeyType const& keyType,
        SecretKey const& secretKey,
        std::uint32_t tokenSequence,
        bool revoked = false);

    /**
     * Creates keys whose secret is held by an external signer.
     *
     * The key file is written with `"secret_key": "external"`.
     */
    SigningKeys(
        KeyType const& keyType,
        PublicKey const& publicKey,
        std::uint32_t tokenSequence = 0,
        bool revoked = false);

    /**
     * Returns SigningKeys constructed from a JSON key file.
     *
     * @param keyFile Path to JSON key file
     *
     * @throws std::runtime_error if file content is invalid
     */
    static SigningKeys
    make_SigningKeys(boost::filesystem::path const& keyFile);

    ~SigningKeys() = default;
    SigningKeys(SigningKeys const&) = default;
    SigningKeys&
    operator=(SigningKeys const&) = delete;

    inline bool
    operator==(SigningKeys const& rhs) const
    {
        return revoked_ == rhs.revoked_ && keyType_ == rhs.keyType_ &&
            tokenSequence_ == rhs.tokenSequence_ && keys_.publicKey == rhs.keys_.publicKey &&
            keys_.secretKey.has_value() == rhs.keys_.secretKey.has_value() &&
            (!keys_.secretKey ||
             std::equal(
                 keys_.secretKey->begin(), keys_.secretKey->end(), rhs.keys_.secretKey->begin()));
    }

    /**
     * Writes the keys to a JSON key file.
     *
     * @param keyFile Path to file to write
     *
     * @note Overwrites an existing key file
     *
     * @throws std::runtime_error if unable to create the parent directory
     */
    void
    writeToFile(boost::filesystem::path const& keyFile) const;

    /**
     * Returns a validator token for the next sequence.
     *
     * @param keyType Key type of the token's signing key
     *
     * @return The token, or nullopt if the keys are revoked or the sequence is
     *         exhausted
     *
     * @throws std::runtime_error if the master key is external
     */
    std::optional<ValidatorToken>
    createValidatorToken(KeyType const& keyType = KeyType::Secp256k1);

    /**
     * Starts a token whose master signature comes from an external signer.
     *
     * When @p externalSigningKey is set, the token delegates to that key and
     * its signature must also come from the external signer, so the returned
     * bytes are signed twice: once by the signing key and once by the master
     * key. Otherwise a software signing key is generated and kept pending in
     * the key file until `finishToken`.
     *
     * @param keyType Key type of a generated signing key; ignored when
     *                @p externalSigningKey is set
     * @param externalSigningKey Signing key held by the external signer
     *
     * @return The hex bytes to sign, or nullopt if the keys are revoked or
     *         the sequence is exhausted
     */
    std::optional<std::string>
    startValidatorToken(
        KeyType const& keyType = KeyType::Secp256k1,
        std::optional<PublicKey> const& externalSigningKey = std::nullopt) const;

    /**
     * Finishes a token started with a generated signing key.
     *
     * @param masterSig Master signature over the bytes `startValidatorToken`
     *                  returned
     *
     * @return The token, or nullopt if the keys are revoked
     *
     * @throws std::runtime_error if no such token is pending or the
     *         signature does not verify
     */
    std::optional<ValidatorToken>
    finishToken(Blob const& masterSig);

    /**
     * Finishes a token started with an external signing key.
     *
     * @param masterSig Master signature over the bytes `startValidatorToken`
     *                  returned
     * @param signingSig Signing-key signature over the same bytes
     *
     * @return The base64 manifest, or nullopt if the keys are revoked
     *
     * @throws std::runtime_error if no such token is pending or a signature
     *         does not verify
     */
    std::optional<std::string>
    finishExternalToken(Blob const& masterSig, Blob const& signingSig);

    /**
     * Revokes the keys.
     *
     * @return The base64 revocation manifest
     *
     * @throws std::runtime_error if the master key is external
     */
    std::string
    revoke();

    /**
     * Starts a revocation whose master signature comes from an external
     * signer.
     *
     * @return The hex bytes to sign
     */
    std::string
    startRevoke() const;

    /**
     * Finishes a revocation.
     *
     * @param masterSig Master signature over the bytes `startRevoke` returned
     *
     * @return The base64 revocation manifest
     *
     * @throws std::runtime_error if the signature does not verify
     */
    std::string
    finishRevoke(Blob const& masterSig);

    /**
     * Signs a string with the master key.
     *
     * @param data String to sign
     *
     * @return The hex signature
     *
     * @throws std::runtime_error if the master key is external
     */
    std::string
    sign(std::string const& data) const;

    /**
     * Signs hex-encoded bytes with the master key.
     *
     * @param data Hex string; decoded to raw bytes before signing
     *
     * @return The hex signature
     *
     * @throws std::runtime_error if the master key is external
     */
    std::string
    signHex(std::string data) const;

    /**
     * Returns the public key.
     */
    PublicKey const&
    publicKey() const
    {
        return keys_.publicKey;
    }

    /**
     * Returns true if the keys are revoked.
     */
    bool
    revoked() const
    {
        return revoked_;
    }

    /**
     * Returns the domain associated with this key, if any.
     */
    std::string const&
    domain() const
    {
        return domain_;
    }

    /**
     * Sets the domain associated with this key.
     */
    void
    domain(std::string d);

    /**
     * Checks the stored manifest.
     *
     * @throws std::runtime_error if the manifest is malformed or not signed
     *         correctly
     */
    void
    verifyManifest() const;

    /**
     * Returns the last manifest generated, if available.
     */
    std::vector<std::uint8_t>
    manifest() const
    {
        if (!manifest_.empty())
            verifyManifest();

        return manifest_;
    }

    /**
     * Returns the sequence number of the last manifest generated.
     */
    std::uint32_t
    sequence() const
    {
        return tokenSequence_;
    }

private:
    void
    setManifest(STObject const& st);

    void
    clearPending() const;
};

}  // namespace xrpl
