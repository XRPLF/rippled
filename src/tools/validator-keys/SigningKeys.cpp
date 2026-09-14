#include <tools/validator-keys/SigningKeys.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/regex.hpp>

#include <fstream>

namespace xrpl {

std::string
tokenToBase64(ValidatorToken const& token)
{
    json::Value jv;
    jv["validation_secret_key"] = strHex(token.validationSecret);
    jv["manifest"] = token.manifest;

    return xrpl::base64Encode(to_string(jv));
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

SigningKeys
SigningKeys::make_SigningKeys(boost::filesystem::path const& keyFile)
{
    std::ifstream ifsKeys(keyFile.c_str(), std::ios::in);

    if (!ifsKeys)
        throw std::runtime_error("Failed to open key file: " + keyFile.string());

    json::Reader reader;
    json::Value jKeys;
    if (!reader.parse(ifsKeys, jKeys))
    {
        throw std::runtime_error("Unable to parse json key file: " + keyFile.string());
    }

    static std::array<std::string, 4> const requiredFields{
        {"key_type", "secret_key", "token_sequence", "revoked"}};

    for (auto field : requiredFields)
    {
        if (!jKeys.isMember(field))
        {
            throw std::runtime_error(
                "Key file '" + keyFile.string() + "' is missing \"" + field + "\" field");
        }
    }

    auto const invalidField = [&keyFile, &jKeys](std::string const& field) {
        return std::runtime_error(
            "Key file '" + keyFile.string() + "' contains invalid \"" + field +
            "\" field: " + jKeys[field].toStyledString());
    };

    auto const keyType = keyTypeFromString(jKeys["key_type"].asString());
    if (!keyType)
        throw invalidField("key_type");

    auto const secret =
        parseBase58<SecretKey>(TokenType::NodePrivate, jKeys["secret_key"].asString());

    auto const pubKey = [&]() -> std::optional<PublicKey> {
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
            return pubKey;
        }
        if (!secret)
            throw invalidField("secret_key");
        return std::nullopt;
    }();

    std::uint32_t tokenSequence;
    try
    {
        if (!jKeys["token_sequence"].isIntegral())
            throw std::runtime_error("");

        tokenSequence = jKeys["token_sequence"].asUInt();
    }
    catch (std::runtime_error&)
    {
        throw invalidField("token_sequence");
    }

    if (!jKeys["revoked"].isBool())
        throw invalidField("revoked");

    SigningKeys vk = [&]() {
        if (secret)
            return SigningKeys(*keyType, *secret, tokenSequence, jKeys["revoked"].asBool());

        if (*keyType != *publicKeyType(*pubKey))
            throw std::runtime_error(
                "Key file '" + keyFile.string() +
                "' has a \"key_type\" that does not match \"public_key\"");
        return SigningKeys(*keyType, *pubKey, tokenSequence, jKeys["revoked"].asBool());
    }();

    if (jKeys.isMember("domain"))
    {
        if (!jKeys["domain"].isString())
            throw invalidField("domain");

        vk.domain(jKeys["domain"].asString());
    }

    if (jKeys.isMember("manifest"))
    {
        if (!jKeys["manifest"].isString())
            throw invalidField("manifest");

        auto ret = strUnHex(jKeys["manifest"].asString());

        if (!ret || ret->size() == 0)
            throw invalidField("manifest");

        vk.manifest_.clear();
        vk.manifest_.reserve(ret->size());
        std::copy(ret->begin(), ret->end(), std::back_inserter(vk.manifest_));
    }

    if (jKeys.isMember("pending_token_secret"))
    {
        if (!jKeys["pending_token_secret"].isString())
            throw invalidField("pending_token_secret");

        vk.pendingTokenSecret_ = parseBase58<SecretKey>(
            TokenType::NodePrivate, jKeys["pending_token_secret"].asString());

        if (!vk.pendingTokenSecret_)
            throw invalidField("pending_token_secret");
    }

    if (jKeys.isMember("pending_signing_key"))
    {
        if (!jKeys["pending_signing_key"].isString())
            throw invalidField("pending_signing_key");

        vk.pendingSigningKey_ =
            parseBase58<PublicKey>(TokenType::NodePublic, jKeys["pending_signing_key"].asString());

        if (!vk.pendingSigningKey_)
            throw invalidField("pending_signing_key");
    }

    if (jKeys.isMember("pending_key_type"))
    {
        auto const pendingKeyType = keyTypeFromString(jKeys["pending_key_type"].asString());
        if (!pendingKeyType)
            throw invalidField("pending_key_type");
        vk.pendingKeyType_ = pendingKeyType;
    }

    return vk;
}

void
SigningKeys::writeToFile(boost::filesystem::path const& keyFile) const
{
    using namespace boost::filesystem;

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
    if (pendingTokenSecret_)
        jv["pending_token_secret"] = toBase58(TokenType::NodePrivate, *pendingTokenSecret_);
    if (pendingSigningKey_)
        jv["pending_signing_key"] = toBase58(TokenType::NodePublic, *pendingSigningKey_);
    if (pendingKeyType_)
        jv["pending_key_type"] = to_string(*pendingKeyType_);

    if (!keyFile.parent_path().empty())
    {
        boost::system::error_code ec;
        if (!exists(keyFile.parent_path()))
            boost::filesystem::create_directories(keyFile.parent_path(), ec);

        if (ec || !is_directory(keyFile.parent_path()))
            throw std::runtime_error("Cannot create directory: " + keyFile.parent_path().string());
    }

    std::ofstream o(keyFile.string(), std::ios_base::trunc);
    if (o.fail())
        throw std::runtime_error("Cannot open key file: " + keyFile.string());

    o << jv.toStyledString();
}

void
SigningKeys::verifyManifest() const
{
    STObject st(sfGeneric);
    SerialIter sit(manifest_.data(), manifest_.size());
    st.set(sit);

    auto fail = []() { throw std::runtime_error("Manifest is not properly signed"); };
    auto const tpk = get<PublicKey>(st, sfSigningPubKey);
    if (revoked() && tpk)
        fail();

    if (!revoked() && (!tpk || !verify(st, HashPrefix::Manifest, *tpk)))
        fail();

    auto const pk = get<PublicKey>(st, sfPublicKey);
    if (!pk || *pk != keys_.publicKey || !verify(st, HashPrefix::Manifest, *pk, sfMasterSignature))
        fail();
}

namespace {

[[nodiscard]] STObject
generatePartialManifest(
    std::uint32_t sequence,
    PublicKey const& masterPubKey,
    PublicKey const& signingPubKey,
    std::string const& domain)
{
    STObject st(sfGeneric);
    st[sfSequence] = sequence;
    st[sfPublicKey] = masterPubKey;
    st[sfSigningPubKey] = signingPubKey;

    if (!domain.empty())
        st[sfDomain] = makeSlice(domain);

    return st;
}

[[nodiscard]] STObject
generatePartialRevocation(PublicKey const& masterPubKey)
{
    STObject st(sfGeneric);
    st[sfSequence] = std::numeric_limits<std::uint32_t>::max();
    st[sfPublicKey] = masterPubKey;

    return st;
}

// The bytes both the signing key and the master key sign.
[[nodiscard]] std::string
signingData(STObject const& st)
{
    Serializer s;
    s.add32(HashPrefix::Manifest);
    st.addWithoutSigningFields(s);
    return strHex(s.peekData());
}

}  // namespace

std::optional<ValidatorToken>
SigningKeys::createValidatorToken(KeyType const& keyType)
{
    if (revoked() || std::numeric_limits<std::uint32_t>::max() - 1 <= tokenSequence_)
        return std::nullopt;

    if (!keys_.secretKey)
        throw std::runtime_error("This key file cannot be used to sign tokens.");

    ++tokenSequence_;

    auto const tokenSecret = generateSecretKey(keyType, randomSeed());
    auto const tokenPublic = derivePublicKey(keyType, tokenSecret);

    STObject st = generatePartialManifest(tokenSequence_, keys_.publicKey, tokenPublic, domain_);

    xrpl::sign(st, HashPrefix::Manifest, keyType, tokenSecret);
    xrpl::sign(st, HashPrefix::Manifest, keyType_, *keys_.secretKey, sfMasterSignature);

    setManifest(st);

    return ValidatorToken{xrpl::base64Encode(manifest_.data(), manifest_.size()), tokenSecret};
}

std::optional<std::string>
SigningKeys::startValidatorToken(
    KeyType const& keyType,
    std::optional<PublicKey> const& externalSigningKey) const
{
    if (revoked() || std::numeric_limits<std::uint32_t>::max() - 1 <= tokenSequence_)
        return std::nullopt;

    clearPending();

    // The next manifest carries the next sequence, but the sequence is not
    // consumed until the signature comes back.
    if (externalSigningKey)
    {
        pendingSigningKey_ = externalSigningKey;
        pendingKeyType_ = publicKeyType(*externalSigningKey);
        return signingData(generatePartialManifest(
            tokenSequence_ + 1, keys_.publicKey, *externalSigningKey, domain_));
    }

    auto const tokenSecret = generateSecretKey(keyType, randomSeed());
    auto const tokenPublic = derivePublicKey(keyType, tokenSecret);

    pendingTokenSecret_ = tokenSecret;
    pendingKeyType_ = keyType;

    return signingData(
        generatePartialManifest(tokenSequence_ + 1, keys_.publicKey, tokenPublic, domain_));
}

ValidatorToken
SigningKeys::finishToken(Blob const& masterSig)
{
    if (revoked())
        throw std::runtime_error("Validator keys have been revoked.");

    if (!pendingTokenSecret_ || !pendingKeyType_)
        throw std::runtime_error("No pending token to finish");

    ++tokenSequence_;

    auto const tokenSecret = *pendingTokenSecret_;
    auto const tokenPublic = derivePublicKey(*pendingKeyType_, tokenSecret);

    STObject st = generatePartialManifest(tokenSequence_, keys_.publicKey, tokenPublic, domain_);

    xrpl::sign(st, HashPrefix::Manifest, *pendingKeyType_, tokenSecret);
    st[sfMasterSignature] = makeSlice(masterSig);

    setManifest(st);

    return ValidatorToken{xrpl::base64Encode(manifest_.data(), manifest_.size()), tokenSecret};
}

std::string
SigningKeys::finishExternalToken(Blob const& masterSig, Blob const& signingSig)
{
    if (revoked())
        throw std::runtime_error("Validator keys have been revoked.");

    if (!pendingSigningKey_)
        throw std::runtime_error("No pending token with an external signing key to finish");

    ++tokenSequence_;

    STObject st =
        generatePartialManifest(tokenSequence_, keys_.publicKey, *pendingSigningKey_, domain_);

    st[sfSignature] = makeSlice(signingSig);
    st[sfMasterSignature] = makeSlice(masterSig);

    setManifest(st);

    return xrpl::base64Encode(manifest_.data(), manifest_.size());
}

std::string
SigningKeys::revoke()
{
    if (!keys_.secretKey)
        throw std::runtime_error("This key file cannot be used to sign tokens.");

    revoked_ = true;

    STObject st = generatePartialRevocation(keys_.publicKey);

    xrpl::sign(st, HashPrefix::Manifest, keyType_, *keys_.secretKey, sfMasterSignature);

    setManifest(st);

    return xrpl::base64Encode(manifest_.data(), manifest_.size());
}

std::string
SigningKeys::startRevoke() const
{
    clearPending();
    return signingData(generatePartialRevocation(keys_.publicKey));
}

std::string
SigningKeys::finishRevoke(Blob const& masterSig)
{
    revoked_ = true;

    STObject st = generatePartialRevocation(keys_.publicKey);

    st[sfMasterSignature] = makeSlice(masterSig);

    setManifest(st);

    return xrpl::base64Encode(manifest_.data(), manifest_.size());
}

void
SigningKeys::setManifest(STObject const& st)
{
    Serializer s;
    st.add(s);

    manifest_.clear();
    manifest_.reserve(s.size());
    std::copy(s.begin(), s.end(), std::back_inserter(manifest_));

    verifyManifest();

    clearPending();
}

void
SigningKeys::clearPending() const
{
    pendingTokenSecret_.reset();
    pendingSigningKey_.reset();
    pendingKeyType_.reset();
}

std::string
SigningKeys::sign(std::string const& data) const
{
    if (!keys_.secretKey)
        throw std::runtime_error("This key file cannot be used to sign.");

    return strHex(xrpl::sign(keys_.publicKey, *keys_.secretKey, makeSlice(data)));
}

std::string
SigningKeys::signHex(std::string data) const
{
    if (!keys_.secretKey)
        throw std::runtime_error("This key file cannot be used to sign.");

    boost::algorithm::trim(data);
    auto const blob = strUnHex(data);
    if (!blob)
        throw std::runtime_error("Could not decode hex string: " + data);
    return strHex(xrpl::sign(keys_.publicKey, *keys_.secretKey, makeSlice(*blob)));
}

void
SigningKeys::domain(std::string d)
{
    if (!d.empty())
    {
        // A valid domain for a validator must be at least 4 characters
        // long, should contain at least one . and should not be longer
        // that 128 characters.
        if (d.size() < 4 || d.size() > 128)
            throw std::runtime_error("The domain must be between 4 and 128 characters long.");

        // This regular expression should do a decent job of weeding out
        // obviously wrong domain names but it isn't perfect. It does not
        // really support IDNs. If this turns out to be an issue, a more
        // thorough regex can be used or this check can just be removed.
        static boost::regex const re(
            "^"                   // Beginning of line
            "("                   // Hostname or domain name
            "(?!-)"               //  - must not begin with '-'
            "[a-zA-Z0-9-]{1,63}"  //  - only alphanumeric and '-'
            "(?<!-)"              //  - must not end with '-'
            "\\."                 // segment separator
            ")+"                  // 1 or more segments
            "[A-Za-z]{2,63}"      // TLD
            "$"                   // End of line
            ,
            boost::regex_constants::optimize);

        if (!boost::regex_match(d, re))
            throw std::runtime_error(
                "The domain field must use the '[host.][subdomain.]domain.tld' "
                "format");
    }

    domain_ = std::move(d);
}

}  // namespace xrpl
