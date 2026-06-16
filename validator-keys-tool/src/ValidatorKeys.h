#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/SecretKey.h>

#include <boost/optional.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace boost {
namespace filesystem {
class path;
}
}  // namespace boost

namespace xrpl {

struct ValidatorToken
{
    std::string const manifest;
    SecretKey const secretKey;

    /// Returns base64-encoded JSON object
    std::string
    toString() const;
};

class ValidatorKeys
{
private:
    KeyType keyType_;

    // struct used to contain both public and secret keys
    struct Keys
    {
        PublicKey publicKey;
        SecretKey secretKey;

        Keys() = delete;
        Keys(std::pair<PublicKey, SecretKey> p) : publicKey(p.first), secretKey(p.second)
        {
        }
    };

    std::vector<std::uint8_t> manifest_;
    std::uint32_t tokenSequence_;
    bool revoked_;
    std::string domain_;
    Keys keys_;

public:
    explicit ValidatorKeys(KeyType const& keyType);

    ValidatorKeys(
        KeyType const& keyType,
        SecretKey const& secretKey,
        std::uint32_t sequence,
        bool revoked = false);

    /** Returns ValidatorKeys constructed from JSON file

        @param keyFile Path to JSON key file

        @throws std::runtime_error if file content is invalid
    */
    static ValidatorKeys
    make_ValidatorKeys(boost::filesystem::path const& keyFile);

    ~ValidatorKeys() = default;
    ValidatorKeys(ValidatorKeys const&) = default;
    ValidatorKeys&
    operator=(ValidatorKeys const&) = default;

    inline bool
    operator==(ValidatorKeys const& rhs) const
    {
        // SecretKey::operator== is deleted to discourage non-constant-time
        // comparison. The public key is derived deterministically from the
        // secret key, so comparing public keys is equivalent here.
        return revoked_ == rhs.revoked_ && keyType_ == rhs.keyType_ &&
            tokenSequence_ == rhs.tokenSequence_ && keys_.publicKey == rhs.keys_.publicKey;
    }

    /** Write keys to JSON file

        @param keyFile Path to file to write

        @note Overwrites existing key file

        @throws std::runtime_error if unable to create parent directory
    */
    void
    writeToFile(boost::filesystem::path const& keyFile) const;

    /** Returns validator token for current sequence

        @param keyType Key type for the token keys
    */
    boost::optional<ValidatorToken>
    createValidatorToken(KeyType const& keyType = KeyType::Secp256k1);

    /** Revokes validator keys

        @return base64-encoded key revocation
    */
    std::string
    revoke();

    /** Signs string with validator key

    @param data String to sign

    @return hex-encoded signature
    */
    std::string
    sign(std::string const& data) const;

    /** Returns the public key. */
    PublicKey const&
    publicKey() const
    {
        return keys_.publicKey;
    }

    /** Returns true if keys are revoked. */
    bool
    revoked() const
    {
        return revoked_;
    }

    /** Returns the domain associated with this key, if any */
    std::string
    domain() const
    {
        return domain_;
    }

    /** Sets the domain associated with this key */
    void
    domain(std::string d);

    /** Returns the last manifest we generated for this domain, if available. */
    std::vector<std::uint8_t>
    manifest() const
    {
        return manifest_;
    }

    /** Returns the sequence number of the last manifest generated. */
    std::uint32_t
    sequence() const
    {
        return tokenSequence_;
    }
};

}  // namespace xrpl
