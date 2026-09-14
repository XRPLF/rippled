#pragma once

#include <xrpl/basics/Blob.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/server/Manifest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace xrpl {

/**
 * Returns the token as the base64 JSON object written to [validator_token].
 */
std::string
tokenToBase64(ValidatorToken const& token);

/**
 * The master key of a validator or a validator-list publisher, as stored in
 * the key file, with the manifests, tokens and revocations the master key
 * signs.
 *
 * A manifest is made in two steps so the master signature can come from a
 * signer outside this process: `startToken` fixes the manifest's contents and
 * returns the bytes to sign, `finishToken` takes the signature back. When the
 * master secret is in the key file, `createToken` does both. A revocation
 * follows the same two steps.
 *
 * The secret key is optional. When it is absent the key file was created with
 * `create_external` and the master key never signs inside this process.
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

    // A token started and not yet finished: the key type of its signing key
    // and either that key's secret, generated here, or its public key when an
    // external signer holds it.
    struct Pending
    {
        KeyType keyType;
        std::variant<SecretKey, PublicKey> signer;
    };

    KeyType const keyType_;
    Keys const keys_;
    std::vector<std::uint8_t> manifest_;
    std::uint32_t tokenSequence_;
    bool revoked_;
    std::string domain_;
    std::optional<Pending> pending_;

public:
    /**
     * The result of finishing a token: the manifest and, when the signing key
     * was generated here, its secret.
     */
    struct Finished
    {
        std::string manifest;
        std::optional<SecretKey> secret;
    };

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
     * @throws std::runtime_error if file content is invalid or the stored
     *         manifest is not a valid manifest for this key
     */
    static SigningKeys
    make_SigningKeys(std::filesystem::path const& keyFile);

    ~SigningKeys() = default;
    SigningKeys(SigningKeys const&) = default;
    SigningKeys&
    operator=(SigningKeys const&) = delete;

    bool
    operator==(SigningKeys const& rhs) const;

    /**
     * Writes the keys to a JSON key file readable by its owner only. The file
     * is replaced whole, so a failed write leaves the previous content.
     *
     * @param keyFile Path to file to write
     *
     * @throws std::runtime_error if the file cannot be written
     */
    void
    writeToFile(std::filesystem::path const& keyFile) const;

    /**
     * Starts a token: fixes the next manifest's contents and returns the
     * bytes both its signatures cover, as hex.
     *
     * With @p externalSigningKey the token delegates to that key and its
     * signature must come from outside too; otherwise a signing key of
     * @p keyType is generated and kept pending until `finishToken`.
     *
     * @throws std::runtime_error if the keys are revoked, the sequence is
     *         exhausted, or the external signing key is the master key
     */
    std::string
    startToken(
        KeyType const& keyType = KeyType::Secp256k1,
        std::optional<PublicKey> const& externalSigningKey = std::nullopt);

    /**
     * Finishes the pending token with the master signature and, when the
     * signing key is external, the signing key's signature over the same
     * bytes.
     *
     * @throws std::runtime_error if no token is pending, a needed signature is
     *         missing, or the manifest does not verify
     */
    Finished
    finishToken(Blob const& masterSig, std::optional<Blob> const& signingSig = std::nullopt);

    /**
     * Makes a token signed with the master secret in this key file.
     *
     * @param keyType Key type of the token's signing key
     *
     * @throws std::runtime_error if the master key is external, the keys are
     *         revoked, or the sequence is exhausted
     */
    ValidatorToken
    createToken(KeyType const& keyType = KeyType::Secp256k1);

    /**
     * Returns the bytes a master signature over a revocation covers, as hex.
     */
    std::string
    startRevoke() const;

    /**
     * Records the revocation and returns the base64 revocation manifest.
     *
     * @throws std::runtime_error if the signature does not verify
     */
    std::string
    finishRevoke(Blob const& masterSig);

    /**
     * Revokes the keys with the master secret in this key file.
     *
     * @throws std::runtime_error if the master key is external
     */
    std::string
    revoke();

    /**
     * Signs a string with the master key.
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
     * @return The hex signature
     *
     * @throws std::runtime_error if the data is not hex or the master key is
     *         external
     */
    std::string
    signHex(std::string data) const;

    /**
     * The string a domain attestation signs, for the domain of this key.
     */
    std::string
    attestationData() const;

    PublicKey const&
    publicKey() const
    {
        return keys_.publicKey;
    }

    /**
     * True when the master secret is in the key file.
     */
    bool
    hasSecret() const
    {
        return keys_.secretKey.has_value();
    }

    bool
    revoked() const
    {
        return revoked_;
    }

    std::string const&
    domain() const
    {
        return domain_;
    }

    /**
     * Sets the domain the next manifest carries.
     *
     * @throws std::runtime_error if the domain is not well formed
     */
    void
    domain(std::string d);

    /**
     * The last manifest generated, serialized; empty if none.
     */
    std::vector<std::uint8_t> const&
    manifest() const
    {
        return manifest_;
    }

    std::uint32_t
    sequence() const
    {
        return tokenSequence_;
    }

private:
    STObject
    partialManifest(std::uint32_t sequence, PublicKey const& signingKey) const;

    STObject
    partialRevocation() const;

    void
    storeManifest(STObject const& st);

    void
    checkManifest() const;
};

}  // namespace xrpl
