#include <tools/validator-keys/SigningKeys.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/tokens.h>
#include <xrpl/server/Manifest.h>

#include <boost/algorithm/string.hpp>

#include <tools/validator-keys/OwnerOnlyFile.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

// Key files are small; anything larger is not one.
constexpr std::size_t kMaxKeyFileBytes = 64 * 1024;

char const* const kRevokedError = "Validator keys have been revoked.";
char const* const kExhaustedError =
    "Maximum number of tokens have already been generated.\n"
    "Revoke validator keys if previous token has been compromised.";
char const* const kNoSecretError = "This key file cannot be used to sign.";
char const* const kBadManifestError = "Manifest is not properly signed";

bool
sameSecret(SecretKey const& a, SecretKey const& b)
{
    return std::equal(a.begin(), a.end(), b.begin());
}

}  // namespace

std::string
tokenToBase64(ValidatorToken const& token)
{
    json::Value jv;
    jv["validation_secret_key"] = strHex(token.validationSecret);
    jv["manifest"] = token.manifest;

    return base64Encode(to_string(jv));
}

SigningKeys::SigningKeys(KeyType const& keyType)
    : keyType_(keyType)
    , keys_(generateKeyPair(keyType_, randomSeed()))
    , tokenSequence_(0)
    , revoked_(false)
{
}

SigningKeys::SigningKeys(
    KeyType const& keyType,
    SecretKey const& secretKey,
    std::uint32_t tokenSequence,
    bool revoked)
    : keyType_(keyType)
    , keys_({derivePublicKey(keyType_, secretKey), secretKey})
    , tokenSequence_(tokenSequence)
    , revoked_(revoked)
{
}

SigningKeys::SigningKeys(
    KeyType const& keyType,
    PublicKey const& publicKey,
    std::uint32_t tokenSequence,
    bool revoked)
    : keyType_(keyType), keys_(publicKey), tokenSequence_(tokenSequence), revoked_(revoked)
{
}

bool
SigningKeys::operator==(SigningKeys const& rhs) const
{
    if (keyType_ != rhs.keyType_ || keys_.publicKey != rhs.keys_.publicKey ||
        keys_.secretKey.has_value() != rhs.keys_.secretKey.has_value() ||
        (keys_.secretKey && !sameSecret(*keys_.secretKey, *rhs.keys_.secretKey)))
        return false;
    if (tokenSequence_ != rhs.tokenSequence_ || revoked_ != rhs.revoked_ ||
        domain_ != rhs.domain_ || manifest_ != rhs.manifest_ ||
        pending_.has_value() != rhs.pending_.has_value())
        return false;
    if (!pending_)
        return true;
    auto const& pending = *pending_;
    auto const& other = *rhs.pending_;
    if (pending.signingKey != other.signingKey ||
        pending.generated.has_value() != other.generated.has_value())
        return false;
    if (!pending.generated || !other.generated)
        return true;
    return pending.generated->keyType == other.generated->keyType &&
        sameSecret(pending.generated->secretKey, other.generated->secretKey);
}

SigningKeys
SigningKeys::makeSigningKeys(std::filesystem::path const& keyFile)
{
    std::error_code ec;
    auto const text = getFileContents(ec, keyFile, kMaxKeyFileBytes);
    if (ec)
        throw std::runtime_error("Failed to open key file: " + keyFile.string());

    json::Reader reader;
    json::Value jKeys;
    if (!reader.parse(text, jKeys) || !jKeys.isObject())
        throw std::runtime_error("Unable to parse json key file: " + keyFile.string());

    static constexpr std::array<char const*, 4> kRequiredFields{
        {"key_type", "secret_key", "token_sequence", "revoked"}};

    for (auto const* field : kRequiredFields)
    {
        if (!jKeys.isMember(field))
        {
            throw std::runtime_error(
                "Key file '" + keyFile.string() + "' is missing \"" + field + "\" field");
        }
    }

    // The value is not repeated: it may be a secret.
    auto const invalidField = [&keyFile](std::string const& field) {
        return std::runtime_error(
            "Key file '" + keyFile.string() + "' contains invalid \"" + field + "\" field");
    };

    auto const keyType = keyTypeFromString(jKeys["key_type"].asString());
    if (!keyType)
        throw invalidField("key_type");

    if (!jKeys["token_sequence"].isIntegral() || jKeys["token_sequence"].isBool() ||
        (jKeys["token_sequence"].isInt() && jKeys["token_sequence"].asInt() < 0))
        throw invalidField("token_sequence");
    auto const tokenSequence = jKeys["token_sequence"].asUInt();

    if (!jKeys["revoked"].isBool())
        throw invalidField("revoked");
    auto const revoked = jKeys["revoked"].asBool();

    auto keys = [&]() {
        if (jKeys["secret_key"].asString() == "external")
        {
            if (!jKeys.isMember("public_key"))
            {
                throw std::runtime_error(
                    "Key file '" + keyFile.string() + "' is missing \"public_key\" field");
            }
            auto const pubKey =
                parseBase58<PublicKey>(TokenType::NodePublic, jKeys["public_key"].asString());
            if (!pubKey)
                throw invalidField("public_key");
            if (*keyType != *publicKeyType(*pubKey))
            {
                throw std::runtime_error(
                    "Key file '" + keyFile.string() +
                    R"(' has a "key_type" that does not match "public_key")");
            }
            return SigningKeys(*keyType, *pubKey, tokenSequence, revoked);
        }
        auto const secret =
            parseBase58<SecretKey>(TokenType::NodePrivate, jKeys["secret_key"].asString());
        if (!secret)
            throw invalidField("secret_key");
        return SigningKeys(*keyType, *secret, tokenSequence, revoked);
    }();

    if (jKeys.isMember("domain"))
    {
        if (!jKeys["domain"].isString())
            throw invalidField("domain");
        keys.domain(jKeys["domain"].asString());
    }

    if (jKeys.isMember("manifest"))
    {
        if (!jKeys["manifest"].isString())
            throw invalidField("manifest");
        auto bytes = strUnHex(jKeys["manifest"].asString());
        if (!bytes || bytes->empty())
            throw invalidField("manifest");
        keys.manifest_ = std::move(*bytes);
        keys.checkManifest();
    }

    bool const hasSecret = jKeys.isMember("pending_token_secret");
    bool const hasSigningKey = jKeys.isMember("pending_signing_key");
    if (hasSecret || hasSigningKey)
    {
        if (hasSecret && hasSigningKey)
        {
            throw std::runtime_error(
                "Key file '" + keyFile.string() +
                R"(' has both "pending_token_secret" and "pending_signing_key")");
        }
        if (hasSecret)
        {
            if (!jKeys.isMember("pending_key_type"))
            {
                throw std::runtime_error(
                    "Key file '" + keyFile.string() + "' is missing \"pending_key_type\" field");
            }
            auto const pendingKeyType = keyTypeFromString(jKeys["pending_key_type"].asString());
            if (!pendingKeyType)
                throw invalidField("pending_key_type");
            if (!jKeys["pending_token_secret"].isString())
                throw invalidField("pending_token_secret");
            auto const secret = parseBase58<SecretKey>(
                TokenType::NodePrivate, jKeys["pending_token_secret"].asString());
            if (!secret)
                throw invalidField("pending_token_secret");
            keys.pending_ = Pending{
                .signingKey = derivePublicKey(*pendingKeyType, *secret),
                .generated = GeneratedKey{.keyType = *pendingKeyType, .secretKey = *secret}};
        }
        else
        {
            if (!jKeys["pending_signing_key"].isString())
                throw invalidField("pending_signing_key");
            auto const signingKey = parseBase58<PublicKey>(
                TokenType::NodePublic, jKeys["pending_signing_key"].asString());
            if (!signingKey)
                throw invalidField("pending_signing_key");
            keys.pending_ = Pending{.signingKey = *signingKey, .generated = std::nullopt};
        }
    }

    return keys;
}

void
SigningKeys::writeToFile(std::filesystem::path const& keyFile) const
{
    namespace fs = std::filesystem;

    json::Value jv;
    jv["key_type"] = to_string(keyType_);
    jv["public_key"] = toBase58(TokenType::NodePublic, keys_.publicKey);
    jv["secret_key"] =
        keys_.secretKey ? toBase58(TokenType::NodePrivate, *keys_.secretKey) : "external";
    jv["token_sequence"] = json::UInt(tokenSequence_);
    jv["revoked"] = revoked_;
    if (!domain_.empty())
        jv["domain"] = domain_;
    if (!manifest_.empty())
        jv["manifest"] = strHex(makeSlice(manifest_));
    if (pending_)
    {
        if (auto const& generated = pending_->generated)
        {
            jv["pending_key_type"] = to_string(generated->keyType);
            jv["pending_token_secret"] = toBase58(TokenType::NodePrivate, generated->secretKey);
        }
        else
        {
            jv["pending_signing_key"] = toBase58(TokenType::NodePublic, pending_->signingKey);
        }
    }

    std::error_code ec;
    if (auto const parent = keyFile.parent_path(); !parent.empty())
    {
        // A directory made here is the owner's alone.
        if (fs::create_directories(parent, ec) && !ec)
            fs::permissions(parent, fs::perms::owner_all, ec);
        if (ec || !fs::is_directory(parent))
            throw std::runtime_error("Cannot create directory: " + parent.string());
    }

    OwnerOnlyFile file(keyFile, "key file");
    file.write(jv.toStyledString());
    file.commit();
}

STObject
SigningKeys::partialManifest(std::uint32_t sequence, PublicKey const& signingKey) const
{
    return makeManifestFields(keys_.publicKey, signingKey, sequence, domain_);
}

STObject
SigningKeys::partialRevocation() const
{
    return makeRevocationFields(keys_.publicKey);
}

void
SigningKeys::checkManifest() const
{
    auto const m = deserializeManifest(manifest_);
    if (!m || !m->verify() || m->masterKey != keys_.publicKey || m->revoked() != revoked_)
        throw std::runtime_error(kBadManifestError);
}

void
SigningKeys::storeManifest(STObject const& st)
{
    Serializer s;
    st.add(s);
    auto const previous = std::exchange(manifest_, std::vector<std::uint8_t>(s.begin(), s.end()));
    try
    {
        checkManifest();
    }
    catch (std::runtime_error const&)
    {
        manifest_ = previous;
        throw;
    }
}

std::string
SigningKeys::startToken(KeyType const& keyType, std::optional<PublicKey> const& externalSigningKey)
{
    if (externalSigningKey)
    {
        if (*externalSigningKey == keys_.publicKey)
            throw std::runtime_error("The signing key must differ from the master key");
        return strHex(
            startPending(Pending{.signingKey = *externalSigningKey, .generated = std::nullopt}));
    }

    auto const secret = generateSecretKey(keyType, randomSeed());
    return strHex(startPending(
        Pending{
            .signingKey = derivePublicKey(keyType, secret),
            .generated = GeneratedKey{.keyType = keyType, .secretKey = secret}}));
}

Blob
SigningKeys::startPending(Pending const& pending)
{
    if (revoked_)
        throw std::runtime_error(kRevokedError);
    if (tokenSequence_ >= std::numeric_limits<std::uint32_t>::max() - 1)
        throw std::runtime_error(kExhaustedError);

    pending_ = pending;
    return manifestSigningData(partialManifest(tokenSequence_ + 1, pending.signingKey));
}

SigningKeys::Finished
SigningKeys::finishToken(Blob const& masterSig, std::optional<Blob> const& signingSig)
{
    if (revoked_)
        throw std::runtime_error(kRevokedError);
    if (!pending_)
        throw std::runtime_error("No pending token to finish");

    auto const pending = *pending_;
    std::optional<SecretKey> secret;
    if (pending.generated)
        secret = pending.generated->secretKey;
    return Finished{.manifest = finishPending(pending, masterSig, signingSig), .secret = secret};
}

std::string
SigningKeys::finishPending(
    Pending const& pending,
    Blob const& masterSig,
    std::optional<Blob> const& signingSig)
{
    STObject st = partialManifest(tokenSequence_ + 1, pending.signingKey);
    if (auto const& generated = pending.generated)
    {
        if (signingSig)
        {
            throw std::runtime_error(
                "The pending token's signing key is in this key file; pass one signature");
        }
        xrpl::sign(st, HashPrefix::Manifest, generated->keyType, generated->secretKey);
    }
    else
    {
        if (!signingSig)
        {
            throw std::runtime_error(
                "The pending token's signing key is external; pass its signature too");
        }
        st[sfSignature] = makeSlice(*signingSig);
    }
    st[sfMasterSignature] = makeSlice(masterSig);

    storeManifest(st);
    ++tokenSequence_;
    pending_.reset();

    return base64Encode(manifest_.data(), manifest_.size());
}

ValidatorToken
SigningKeys::createToken(KeyType const& keyType)
{
    if (!keys_.secretKey)
        throw std::runtime_error("This key file cannot be used to sign tokens.");

    auto const secret = generateSecretKey(keyType, randomSeed());
    Pending const pending{
        .signingKey = derivePublicKey(keyType, secret),
        .generated = GeneratedKey{.keyType = keyType, .secretKey = secret}};
    auto const data = startPending(pending);
    return ValidatorToken{
        .manifest = finishPending(pending, masterSign(makeSlice(data)), std::nullopt),
        .validationSecret = secret};
}

std::string
SigningKeys::startRevoke() const
{
    return strHex(manifestSigningData(partialRevocation()));
}

std::string
SigningKeys::finishRevoke(Blob const& masterSig)
{
    STObject st = partialRevocation();
    st[sfMasterSignature] = makeSlice(masterSig);

    auto const wasRevoked = std::exchange(revoked_, true);
    try
    {
        storeManifest(st);
    }
    catch (std::runtime_error const&)
    {
        revoked_ = wasRevoked;
        throw;
    }
    pending_.reset();

    return base64Encode(manifest_.data(), manifest_.size());
}

std::string
SigningKeys::revoke()
{
    if (!keys_.secretKey)
        throw std::runtime_error("This key file cannot be used to sign tokens.");

    return finishRevoke(masterSign(makeSlice(manifestSigningData(partialRevocation()))));
}

Blob
SigningKeys::masterSign(Slice const& data) const
{
    if (!keys_.secretKey)
        throw std::runtime_error(kNoSecretError);

    auto const sig = xrpl::sign(keys_.publicKey, *keys_.secretKey, data);
    return Blob(sig.begin(), sig.end());
}

std::string
SigningKeys::sign(std::string const& data) const
{
    return strHex(masterSign(makeSlice(data)));
}

std::string
SigningKeys::signHex(std::string data) const
{
    boost::algorithm::trim(data);
    auto const blob = strUnHex(data);
    if (!blob)
        throw std::runtime_error("Could not decode hex string: " + data);
    return strHex(masterSign(makeSlice(*blob)));
}

std::string
SigningKeys::attestationData() const
{
    return "[domain-attestation-blob:" + domain_ + ":" +
        toBase58(TokenType::NodePublic, keys_.publicKey) + "]";
}

void
SigningKeys::domain(std::string d)
{
    if (!d.empty() && !isProperlyFormedTomlDomain(d))
    {
        throw std::runtime_error(
            "The domain field must use the '[host.][subdomain.]domain.tld' format");
    }

    domain_ = std::move(d);
}

}  // namespace xrpl
