#include <tools/validator-keys/ListSigning.h>

#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>

namespace xrpl {

namespace {

// A version 2 list document holds at most this many blobs.
constexpr std::size_t kMaxBlobs = 5;

[[nodiscard]] std::string
readFile(boost::filesystem::path const& file)
{
    std::ifstream in(file.c_str(), std::ios::in | std::ios::binary);
    if (!in)
        throw std::runtime_error("Failed to open file: " + file.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The base64 lines of a config-style block, without its section header or
// comment lines.
[[nodiscard]] std::vector<std::string>
base64Lines(std::string const& text)
{
    std::vector<std::string> lines;
    std::vector<std::string> raw;
    boost::split(raw, text, boost::is_any_of("\n"));
    for (auto line : raw)
    {
        boost::trim(line);
        if (line.empty() || line.front() == '#' || line.front() == '[')
            continue;
        lines.push_back(line);
    }
    return lines;
}

[[nodiscard]] std::optional<PublicKey>
parseHexKey(std::string const& hex)
{
    auto const bytes = strUnHex(hex);
    if (!bytes)
        return std::nullopt;
    auto const slice = makeSlice(*bytes);
    if (!publicKeyType(slice))
        return std::nullopt;
    return PublicKey(slice);
}

[[nodiscard]] std::optional<Manifest>
parseManifest(std::string const& base64)
{
    auto m = deserializeManifest(base64Decode(base64));
    if (!m || !m->verify())
        return std::nullopt;
    return m;
}

[[nodiscard]] std::optional<std::uint32_t>
uintField(json::Value const& obj, char const* name)
{
    if (!obj.isMember(name) || !obj[name].isIntegral() || obj[name].asInt() < 0)
        return std::nullopt;
    return obj[name].asUInt();
}

}  // namespace

ValidatorToken
loadTokenFile(boost::filesystem::path const& tokenFile)
{
    auto const token = loadValidatorToken(base64Lines(readFile(tokenFile)));
    if (!token)
        throw std::runtime_error("Not a validator token: " + tokenFile.string());
    return *token;
}

Manifest
loadManifestFile(boost::filesystem::path const& manifestFile)
{
    auto const lines = base64Lines(readFile(manifestFile));
    std::string base64;
    for (auto const& line : lines)
        base64 += line;
    auto m = parseManifest(base64);
    if (!m)
        throw std::runtime_error("Not a valid manifest: " + manifestFile.string());
    return std::move(*m);
}

std::string
canonicalJson(std::string const& text)
{
    // Reject malformed text before whitespace is moved.
    {
        json::Reader reader;
        json::Value parsed;
        if (!reader.parse(text, parsed) || !parsed.isObject())
            throw std::runtime_error("Not a JSON object");
    }

    std::string out;
    out.reserve(text.size());
    bool inString = false;
    bool escaped = false;
    for (char const c : text)
    {
        if (inString)
        {
            out += c;
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
            continue;
        }
        switch (c)
        {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
                break;
            case '"':
                inString = true;
                out += c;
                break;
            case ',':
                out += ", ";
                break;
            case ':':
                out += ": ";
                break;
            default:
                out += c;
        }
    }
    return out;
}

UnsignedList
parseUnsignedList(std::string const& text)
{
    UnsignedList list;
    list.canonical = canonicalJson(text);

    json::Reader reader;
    json::Value jv;
    reader.parse(list.canonical, jv);

    auto const sequence = uintField(jv, jss::sequence);
    if (!sequence || *sequence == 0)
        throw std::runtime_error("\"sequence\" must be a positive integer");
    list.sequence = *sequence;

    auto const expiration = uintField(jv, jss::expiration);
    if (!expiration)
        throw std::runtime_error("\"expiration\" must be an unsigned integer");
    list.expiration = *expiration;

    if (jv.isMember(jss::effective))
    {
        auto const effective = uintField(jv, jss::effective);
        if (!effective)
            throw std::runtime_error("\"effective\" must be an unsigned integer");
        if (*effective >= list.expiration)
            throw std::runtime_error("\"effective\" must be earlier than \"expiration\"");
        list.effective = effective;
    }

    if (!jv.isMember(jss::validators) || !jv[jss::validators].isArray() ||
        jv[jss::validators].size() == 0)
        throw std::runtime_error("\"validators\" must be a non-empty array");

    for (auto const& entry : jv[jss::validators])
    {
        if (!entry.isObject() || !entry.isMember(jss::validation_public_key) ||
            !entry[jss::validation_public_key].isString())
            throw std::runtime_error("every validator needs a \"validation_public_key\" string");

        auto const key = parseHexKey(entry[jss::validation_public_key].asString());
        if (!key)
            throw std::runtime_error(
                "\"validation_public_key\" is not a hex public key: " +
                entry[jss::validation_public_key].asString());

        if (entry.isMember(jss::manifest))
        {
            if (!entry[jss::manifest].isString())
                throw std::runtime_error(
                    "\"manifest\" must be a base64 string for " +
                    entry[jss::validation_public_key].asString());
            auto const m = parseManifest(entry[jss::manifest].asString());
            if (!m)
                throw std::runtime_error(
                    "\"manifest\" does not verify for " +
                    entry[jss::validation_public_key].asString());
            if (m->masterKey != *key)
                throw std::runtime_error(
                    "\"manifest\" belongs to another key than " +
                    entry[jss::validation_public_key].asString());
        }

        list.validators.push_back(*key);
    }

    return list;
}

UnsignedList
loadUnsignedList(boost::filesystem::path const& file)
{
    return parseUnsignedList(readFile(file));
}

std::string
signList(UnsignedList const& list, PublicKey const& signingKey, SecretKey const& signingSecret)
{
    return strHex(sign(signingKey, signingSecret, makeSlice(list.canonical)));
}

json::Value
makeSignedList(
    std::string const& manifestBase64,
    PublicKey const& masterKey,
    UnsignedList const& list,
    std::string const& signatureHex,
    unsigned version,
    std::optional<json::Value> const& append)
{
    auto const blob = base64Encode(list.canonical);

    if (version == 1)
    {
        if (append)
            throw std::runtime_error("A version 1 list holds one blob; use version 2 to append");
        json::Value jv(json::ValueType::Object);
        jv[jss::blob] = blob;
        jv[jss::manifest] = manifestBase64;
        jv[jss::public_key] = strHex(masterKey);
        jv[jss::signature] = signatureHex;
        jv[jss::version] = 1;
        return jv;
    }

    if (version != 2)
        throw std::runtime_error("Unsupported list version");

    json::Value jv(json::ValueType::Object);
    if (append)
    {
        auto const& existing = *append;
        if (!existing.isObject() || !existing.isMember(jss::version) ||
            !existing[jss::version].isIntegral() || existing[jss::version].asUInt() != 2 ||
            !existing.isMember(jss::blobs_v2) || !existing[jss::blobs_v2].isArray())
            throw std::runtime_error("The list to append to is not a version 2 list");
        if (!existing.isMember(jss::public_key) || !existing[jss::public_key].isString() ||
            !boost::iequals(existing[jss::public_key].asString(), strHex(masterKey)))
            throw std::runtime_error("The list to append to belongs to another master key");
        if (existing[jss::blobs_v2].size() >= kMaxBlobs)
            throw std::runtime_error(
                "The list to append to already holds " + std::to_string(kMaxBlobs) + " blobs");
        jv = existing;
    }
    else
    {
        jv[jss::blobs_v2] = json::Value(json::ValueType::Array);
    }

    jv[jss::manifest] = manifestBase64;
    jv[jss::public_key] = strHex(masterKey);
    jv[jss::version] = 2;

    json::Value entry(json::ValueType::Object);
    entry[jss::blob] = blob;
    entry[jss::signature] = signatureHex;
    jv[jss::blobs_v2].append(entry);
    return jv;
}

std::uint32_t
rippleEpochNow()
{
    using namespace std::chrono;
    auto const since1970 = duration_cast<seconds>(system_clock::now().time_since_epoch());
    return static_cast<std::uint32_t>((since1970 - kEpochOffset).count());
}

ListVerification
verifyList(
    json::Value const& list,
    std::optional<UnsignedList> const& expectedRoster,
    std::optional<PublicKey> const& expectedKey,
    std::uint32_t now)
{
    ListVerification result;
    result.report = json::Value(json::ValueType::Object);
    auto fail = [&result](std::string const& error) {
        result.ok = false;
        result.errors.push_back(error);
    };

    if (!list.isObject())
    {
        fail("the list is not a JSON object");
        return result;
    }

    auto const version = uintField(list, jss::version);
    if (!version || (*version != 1 && *version != 2))
    {
        fail("\"version\" must be 1 or 2");
        return result;
    }
    result.report[jss::version] = *version;

    if (!list.isMember(jss::public_key) || !list[jss::public_key].isString() ||
        !list.isMember(jss::manifest) || !list[jss::manifest].isString())
    {
        fail("\"public_key\" and \"manifest\" must be strings");
        return result;
    }

    auto const manifest = parseManifest(list[jss::manifest].asString());
    if (!manifest)
    {
        fail("\"manifest\" does not deserialize and verify");
        return result;
    }
    result.report[jss::public_key] = strHex(manifest->masterKey);
    result.report["manifest_sequence"] = manifest->sequence;

    if (manifest->revoked() || !manifest->signingKey)
    {
        fail("the publisher's master key is revoked");
        return result;
    }
    result.report["signing_key"] = strHex(*manifest->signingKey);

    {
        auto const declared = parseHexKey(list[jss::public_key].asString());
        if (!declared || *declared != manifest->masterKey)
            fail("\"public_key\" is not the manifest's master key");
    }

    if (expectedKey && *expectedKey != manifest->masterKey)
        fail("the master key is not the expected key");

    // The blobs of either version as (blob, signature) pairs.
    std::vector<std::pair<std::string, std::string>> blobs;
    if (*version == 1)
    {
        if (!list.isMember(jss::blob) || !list[jss::blob].isString() ||
            !list.isMember(jss::signature) || !list[jss::signature].isString() ||
            list.isMember(jss::blobs_v2))
        {
            fail("a version 1 list needs \"blob\" and \"signature\" and no \"blobs_v2\"");
            return result;
        }
        blobs.emplace_back(list[jss::blob].asString(), list[jss::signature].asString());
    }
    else
    {
        if (!list.isMember(jss::blobs_v2) || !list[jss::blobs_v2].isArray() ||
            list[jss::blobs_v2].size() == 0 || list[jss::blobs_v2].size() > kMaxBlobs ||
            list.isMember(jss::blob) || list.isMember(jss::signature))
        {
            fail(
                "a version 2 list needs 1 to " + std::to_string(kMaxBlobs) +
                " \"blobs_v2\" entries and no top-level \"blob\"");
            return result;
        }
        for (auto const& entry : list[jss::blobs_v2])
        {
            if (!entry.isObject() || !entry.isMember(jss::blob) || !entry[jss::blob].isString() ||
                !entry.isMember(jss::signature) || !entry[jss::signature].isString())
            {
                fail("every \"blobs_v2\" entry needs \"blob\" and \"signature\"");
                return result;
            }
            if (entry.isMember(jss::manifest))
            {
                if (!entry[jss::manifest].isString())
                {
                    fail("a \"blobs_v2\" entry's \"manifest\" must be a string");
                    return result;
                }
                auto const m = parseManifest(entry[jss::manifest].asString());
                if (!m || m->masterKey != manifest->masterKey)
                    fail("a \"blobs_v2\" entry's \"manifest\" is not this publisher's");
            }
            blobs.emplace_back(entry[jss::blob].asString(), entry[jss::signature].asString());
        }
    }

    result.report["blobs"] = json::Value(json::ValueType::Array);
    std::size_t index = 0;
    for (auto const& [blob, signature] : blobs)
    {
        auto const where = "blob " + std::to_string(index++);
        json::Value entry(json::ValueType::Object);

        auto const sig = strUnHex(signature);
        auto const data = base64Decode(blob);
        if (!sig || !verify(*manifest->signingKey, makeSlice(data), makeSlice(*sig)))
            fail(where + ": the signature does not verify under the signing key");

        std::optional<UnsignedList> parsed;
        try
        {
            parsed = parseUnsignedList(data);
        }
        catch (std::runtime_error const& e)
        {
            fail(where + ": " + e.what());
        }

        if (parsed)
        {
            entry[jss::sequence] = parsed->sequence;
            if (parsed->effective)
                entry[jss::effective] = *parsed->effective;
            entry[jss::expiration] = parsed->expiration;
            entry[jss::validators] = json::UInt(parsed->validators.size());
            entry["expired"] = parsed->expiration <= now;

            if (parsed->expiration <= now)
                fail(where + ": expired");

            if (expectedRoster)
            {
                std::set<PublicKey> const have(
                    parsed->validators.begin(), parsed->validators.end());
                std::set<PublicKey> const want(
                    expectedRoster->validators.begin(), expectedRoster->validators.end());
                if (have != want)
                    fail(where + ": the validators differ from the expected list");
            }
        }

        result.report["blobs"].append(entry);
    }

    result.report["ok"] = result.ok;
    result.report["errors"] = json::Value(json::ValueType::Array);
    for (auto const& e : result.errors)
        result.report["errors"].append(e);
    return result;
}

}  // namespace xrpl
