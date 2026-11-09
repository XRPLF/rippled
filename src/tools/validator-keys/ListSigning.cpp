#include <tools/validator-keys/ListSigning.h>

#include <xrpl/basics/FileUtilities.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base64.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/Manifest.h>

#include <boost/algorithm/string.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace xrpl {

namespace {

// A version 2 document holds at most this many blobs.
constexpr std::size_t kMaxBlobs = 5;
// The largest value a server reads for sequence, expiration and effective.
constexpr std::int64_t kMaxListInteger = 2147483647;
constexpr std::size_t kMaxListBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxKeyMaterialBytes = 64 * 1024;

std::string
readFile(std::filesystem::path const& file, std::size_t maxSize)
{
    std::error_code ec;
    auto text = getFileContents(ec, file, maxSize);
    if (ec)
        throw std::runtime_error("Failed to open file: " + file.string());
    return text;
}

// A character of a number, `true`, `false` or `null` outside a string.
bool
scalarChar(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' ||
        c == '+' || c == '-';
}

// The base64 lines of a config-style block, without its section header or
// comment lines.
std::vector<std::string>
base64Lines(std::string const& text)
{
    std::vector<std::string> lines;
    boost::split(lines, text, boost::is_any_of("\n"));
    for (auto& line : lines)
        boost::trim(line);
    std::erase_if(lines, [](std::string const& line) {
        return line.empty() || line.front() == '#' || line.front() == '[';
    });
    return lines;
}

std::optional<Manifest>
parseManifest(std::string const& base64)
{
    if (base64.size() > kMaxManifestBase64)
        return std::nullopt;
    auto m = deserializeManifest(base64Decode(base64));
    if (!m || !m->verify())
        return std::nullopt;
    return m;
}

// An integer field as a server reads it: type Int, so at most 2147483647.
std::optional<std::uint32_t>
listInteger(json::Value const& obj, char const* name)
{
    if (!obj.isMember(name) || !obj[name].isInt() || obj[name].asInt() < 0)
        return std::nullopt;
    return obj[name].asUInt();
}

std::string
integerError(char const* name, std::int64_t least)
{
    return std::string("\"") + name + "\" must be an integer from " + std::to_string(least) +
        " to " + std::to_string(kMaxListInteger);
}

// Field checks over parsed text whose canonical form is already known.
UnsignedList
checkedList(std::string canonical, json::Value const& jv)
{
    UnsignedList list;
    list.canonical = std::move(canonical);

    auto const sequence = listInteger(jv, jss::sequence);
    if (!sequence || *sequence == 0)
        throw std::runtime_error(integerError(jss::sequence, 1));
    list.sequence = *sequence;

    // A server takes a missing effective time as 0 and needs expiration after it.
    auto const expiration = listInteger(jv, jss::expiration);
    if (!expiration || *expiration == 0)
        throw std::runtime_error(integerError(jss::expiration, 1));
    list.expiration = *expiration;

    if (jv.isMember(jss::effective))
    {
        auto const effective = listInteger(jv, jss::effective);
        if (!effective)
            throw std::runtime_error(integerError(jss::effective, 0));
        if (*effective >= list.expiration)
            throw std::runtime_error(R"("effective" must be earlier than "expiration")");
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

        auto const keyText = entry[jss::validation_public_key].asString();
        auto const key = parseHexKey(keyText);
        if (!key)
        {
            throw std::runtime_error(
                "\"validation_public_key\" is not a hex public key: " + keyText);
        }

        if (entry.isMember(jss::manifest))
        {
            if (!entry[jss::manifest].isString())
                throw std::runtime_error("\"manifest\" must be a base64 string for " + keyText);
            auto const m = parseManifest(entry[jss::manifest].asString());
            if (!m)
                throw std::runtime_error("\"manifest\" does not verify for " + keyText);
            if (m->masterKey != *key)
                throw std::runtime_error("\"manifest\" belongs to another key than " + keyText);
        }

        if (std::ranges::find(list.validators, *key) != list.validators.end())
            throw std::runtime_error("\"validators\" lists " + keyText + " more than once");
        list.validators.push_back(*key);
    }

    return list;
}

json::Value
parseObject(std::string const& text)
{
    json::Reader reader;
    json::Value jv;
    if (!reader.parse(text, jv) || !jv.isObject())
        throw std::runtime_error("Not a JSON object");
    return jv;
}

// A published blob's list, checked as an unsigned list is: the tool copies only
// a blob it would sign itself.
UnsignedList
blobList(std::string const& blobBytes)
{
    try
    {
        return checkedList(blobBytes, parseObject(blobBytes));
    }
    catch (std::runtime_error const& e)
    {
        throw std::runtime_error(
            "The list to append to holds an invalid blob: " + std::string(e.what()));
    }
}

}  // namespace

std::optional<PublicKey>
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

std::optional<std::string>
decodeBase64Exact(std::string const& data)
{
    auto bytes = base64Decode(data);
    if (base64Encode(bytes) != data)
        return std::nullopt;
    return bytes;
}

ValidatorToken
loadTokenFile(std::filesystem::path const& tokenFile)
{
    auto const token = loadValidatorToken(base64Lines(readFile(tokenFile, kMaxKeyMaterialBytes)));
    if (!token)
        throw std::runtime_error("Not a validator token: " + tokenFile.string());
    return *token;
}

Manifest
loadManifestFile(std::filesystem::path const& manifestFile)
{
    auto const lines = base64Lines(readFile(manifestFile, kMaxKeyMaterialBytes));
    auto m = parseManifest(boost::join(lines, ""));
    if (!m)
        throw std::runtime_error("Not a valid manifest: " + manifestFile.string());
    return std::move(*m);
}

std::string
canonicalJson(std::string const& text)
{
    std::string out;
    out.reserve(text.size());
    bool inString = false;
    bool escaped = false;
    // Bracket depth outside strings and comments; json::Reader::parse ignores
    // text after the root value, so once the depth returns to 0 only
    // whitespace and comments may follow.
    std::size_t depth = 0;
    bool closed = false;
    // Whitespace or a comment was skipped since the last emitted character;
    // dropping it between two scalar characters would merge two tokens.
    bool separated = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        char const c = text[i];
        if (inString)
        {
            out += c;
            if (escaped)
            {
                escaped = false;
            }
            else if (c == '\\')
            {
                escaped = true;
            }
            else if (c == '"')
            {
                inString = false;
            }
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            separated = true;
            // json::Reader ends a line comment at CR or LF.
            i = text.find_first_of("\r\n", i);
            if (i == std::string::npos)
                break;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
        {
            separated = true;
            i = text.find("*/", i + 2);
            if (i == std::string::npos)
                throw std::runtime_error("Unterminated comment");
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            separated = true;
            continue;
        }
        if (closed)
            throw std::runtime_error("Content after the JSON object");
        if (separated && scalarChar(c) && !out.empty() && scalarChar(out.back()))
            throw std::runtime_error("Values run together");
        separated = false;
        switch (c)
        {
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
            case '{':
            case '[':
                ++depth;
                out += c;
                break;
            case '}':
            case ']':
                if (depth > 0)
                    closed = --depth == 0;
                out += c;
                break;
            default:
                out += c;
        }
    }
    parseObject(out);
    return out;
}

UnsignedList
parseUnsignedList(std::string const& text)
{
    auto canonical = canonicalJson(text);
    auto const jv = parseObject(canonical);
    return checkedList(std::move(canonical), jv);
}

UnsignedList
loadUnsignedList(std::filesystem::path const& file)
{
    return parseUnsignedList(readFile(file, kMaxListBytes));
}

void
requireUnexpired(UnsignedList const& list, std::uint32_t now)
{
    if (list.expiration <= now)
    {
        throw std::runtime_error(
            R"(The list's "expiration" is not after now; a server would take it as expired)");
    }
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
    std::optional<json::Value> const& append,
    std::uint32_t now)
{
    requireUnexpired(list, now);
    if (version != 1 && version != 2)
        throw std::runtime_error("Unsupported list version");
    if (version == 1 && append)
        throw std::runtime_error("A version 1 list holds one blob; use version 2 to append");

    // The token's manifest must verify and belong to the list's master key.
    auto const current = parseManifest(manifestBase64);
    if (!current || current->masterKey != masterKey || !current->signingKey)
        throw std::runtime_error("The token holds an invalid manifest");

    json::Value jv(json::ValueType::Object);
    if (append)
    {
        auto const& existing = *append;
        if (!existing.isObject() || !existing.isMember(jss::version) ||
            !existing[jss::version].isInt() || existing[jss::version].asInt() != 2 ||
            !existing.isMember(jss::blobs_v2) || !existing[jss::blobs_v2].isArray() ||
            !existing.isMember(jss::manifest) || !existing[jss::manifest].isString())
            throw std::runtime_error("The list to append to is not a version 2 list");
        if (!existing.isMember(jss::public_key) || !existing[jss::public_key].isString() ||
            !boost::iequals(existing[jss::public_key].asString(), strHex(masterKey)))
            throw std::runtime_error("The list to append to belongs to another master key");
        if (existing[jss::blobs_v2].size() >= kMaxBlobs)
        {
            throw std::runtime_error(
                "The list to append to already holds " + std::to_string(kMaxBlobs) + " blobs");
        }

        // Whether a manifest in the document is the token's. A server that
        // applied a manifest rejects an older or equal one, and with it every
        // blob signed under it, so any other manifest must be older; it must
        // also verify for the list's master key, or its blobs were never the
        // publisher's.
        auto const isCurrent = [&](std::string const& base64) {
            if (base64Decode(base64) == current->serialized)
                return true;
            auto const published = parseManifest(base64);
            if (!published)
                throw std::runtime_error("The list to append to holds an invalid manifest");
            if (published->masterKey != masterKey)
            {
                throw std::runtime_error(
                    "The list to append to holds a manifest for another master key");
            }
            if (!published->signingKey)
                throw std::runtime_error("The list to append to holds an invalid manifest");
            if (published->sequence >= current->sequence)
            {
                throw std::runtime_error(
                    "The list to append to was signed under manifest sequence " +
                    std::to_string(published->sequence) +
                    "; append with a token whose manifest sequence is higher");
            }
            return false;
        };
        bool const topCurrent = isCurrent(existing[jss::manifest].asString());

        // A server ignores an expired blob and verify_list fails a document
        // holding one, so expired blobs are left out; their sequences still
        // bound the appended list's.
        json::Value kept(json::ValueType::Array);
        std::uint32_t highest = 0;
        for (auto const& entry : existing[jss::blobs_v2])
        {
            if (!entry.isObject() || !entry.isMember(jss::blob) || !entry[jss::blob].isString() ||
                !entry.isMember(jss::signature) || !entry[jss::signature].isString())
                throw std::runtime_error("The list to append to holds an invalid blob");
            auto const bytes = decodeBase64Exact(entry[jss::blob].asString());
            auto const signature = strUnHex(entry[jss::signature].asString());
            if (!bytes || !signature)
                throw std::runtime_error("The list to append to holds an invalid blob");
            auto entryCurrent = topCurrent;
            if (entry.isMember(jss::manifest))
            {
                if (!entry[jss::manifest].isString())
                    throw std::runtime_error("The list to append to holds an invalid manifest");
                entryCurrent = isCurrent(entry[jss::manifest].asString());
            }
            // A server verifies every blob under the newest manifest it has seen.
            if (!entryCurrent)
            {
                throw std::runtime_error(
                    "The list to append to holds a blob under an older manifest; sign its lists "
                    "again with this token, without --append");
            }
            if (!verify(*current->signingKey, makeSlice(*bytes), makeSlice(*signature)))
            {
                throw std::runtime_error(
                    "The list to append to holds a blob whose signature does not verify");
            }
            auto const published = blobList(*bytes);
            highest = std::max(highest, published.sequence);
            if (published.expiration > now)
                kept.append(entry);
        }
        // A server ignores a blob whose sequence is not above the one it holds.
        if (list.sequence <= highest)
        {
            throw std::runtime_error(
                "The list to append has sequence " + std::to_string(list.sequence) +
                "; the document already holds sequence " + std::to_string(highest));
        }
        jv[jss::blobs_v2] = kept;
    }

    jv[jss::manifest] = manifestBase64;
    jv[jss::public_key] = strHex(masterKey);
    jv[jss::version] = static_cast<int>(version);

    auto const blob = base64Encode(list.canonical);
    if (version == 1)
    {
        jv[jss::blob] = blob;
        jv[jss::signature] = signatureHex;
        return jv;
    }

    json::Value entry(json::ValueType::Object);
    entry[jss::blob] = blob;
    entry[jss::signature] = signatureHex;
    jv[jss::blobs_v2].append(entry);
    return jv;
}

std::uint32_t
netClockNow()
{
    using namespace std::chrono;
    auto const since1970 = duration_cast<seconds>(system_clock::now().time_since_epoch());
    return static_cast<std::uint32_t>((since1970 - kEpochOffset).count());
}

json::Value
verifyList(
    json::Value const& list,
    std::optional<UnsignedList> const& expectedRoster,
    std::optional<PublicKey> const& expectedKey,
    std::uint32_t now)
{
    json::Value report(json::ValueType::Object);
    report["ok"] = true;
    report["errors"] = json::Value(json::ValueType::Array);
    auto const fail = [&report](std::string const& error) {
        report["ok"] = false;
        report["errors"].append(error);
    };

    if (!list.isObject())
    {
        fail("the list is not a JSON object");
        return report;
    }

    auto const version = listInteger(list, jss::version);
    if (!version || (*version != 1 && *version != 2))
    {
        fail("\"version\" must be 1 or 2");
        return report;
    }
    report[jss::version] = *version;

    if (!list.isMember(jss::public_key) || !list[jss::public_key].isString() ||
        !list.isMember(jss::manifest) || !list[jss::manifest].isString())
    {
        fail(R"("public_key" and "manifest" must be strings)");
        return report;
    }

    auto manifest = parseManifest(list[jss::manifest].asString());
    if (!manifest)
    {
        fail("\"manifest\" does not deserialize and verify");
        return report;
    }
    report[jss::public_key] = strHex(manifest->masterKey);
    report["manifest_sequence"] = manifest->sequence;

    if (manifest->revoked() || !manifest->signingKey)
    {
        fail("the publisher's master key is revoked");
        return report;
    }
    report["signing_key"] = strHex(*manifest->signingKey);

    {
        auto const declared = parseHexKey(list[jss::public_key].asString());
        if (!declared || *declared != manifest->masterKey)
            fail("\"public_key\" is not the manifest's master key");
    }
    if (expectedKey && *expectedKey != manifest->masterKey)
        fail("the master key is not the expected key");

    struct Entry
    {
        std::string blob;
        std::string signature;
        std::optional<std::string> manifest;
    };
    std::vector<Entry> entries;
    if (*version == 1)
    {
        if (!list.isMember(jss::blob) || !list[jss::blob].isString() ||
            !list.isMember(jss::signature) || !list[jss::signature].isString() ||
            list.isMember(jss::blobs_v2))
        {
            fail(R"(a version 1 list needs "blob" and "signature" and no "blobs_v2")");
            return report;
        }
        entries.push_back({
            .blob = list[jss::blob].asString(),
            .signature = list[jss::signature].asString(),
            .manifest = {},
        });
    }
    else
    {
        if (!list.isMember(jss::blobs_v2) || !list[jss::blobs_v2].isArray() ||
            list[jss::blobs_v2].size() == 0 || list[jss::blobs_v2].size() > kMaxBlobs ||
            list.isMember(jss::blob) || list.isMember(jss::signature))
        {
            fail(
                "a version 2 list needs 1 to " + std::to_string(kMaxBlobs) +
                R"( "blobs_v2" entries and no top-level "blob")");
            return report;
        }
        for (auto const& entry : list[jss::blobs_v2])
        {
            if (!entry.isObject() || !entry.isMember(jss::blob) || !entry[jss::blob].isString() ||
                !entry.isMember(jss::signature) || !entry[jss::signature].isString() ||
                (entry.isMember(jss::manifest) && !entry[jss::manifest].isString()))
            {
                fail(
                    "every \"blobs_v2\" entry needs \"blob\" and \"signature\" strings and "
                    "an optional \"manifest\" string");
                return report;
            }
            std::optional<std::string> entryManifest;
            if (entry.isMember(jss::manifest))
                entryManifest = entry[jss::manifest].asString();
            entries.push_back({
                .blob = entry[jss::blob].asString(),
                .signature = entry[jss::signature].asString(),
                .manifest = entryManifest,
            });
        }
    }

    std::optional<std::set<PublicKey>> want;
    if (expectedRoster)
        want.emplace(expectedRoster->validators.begin(), expectedRoster->validators.end());

    report["blobs"] = json::Value(json::ValueType::Array);
    // held points at the top-level manifest or at heldOwn, an entry's manifest
    std::optional<Manifest> heldOwn;
    Manifest const* held = nullptr;
    // The sequence and index of the last blob that parsed, for the order check
    std::optional<std::pair<std::uint32_t, std::size_t>> previous;
    std::size_t index = 0;
    for (auto const& entry : entries)
    {
        auto const current = index;
        auto const where = "blob " + std::to_string(index++);
        json::Value found(json::ValueType::Object);

        // Each blob applies its own manifest, else the top-level one, and a
        // server keeps the first applied until a higher sequence replaces it.
        std::optional<Manifest> own;
        if (entry.manifest)
        {
            auto m = parseManifest(*entry.manifest);
            if (!m || m->masterKey != manifest->masterKey)
            {
                fail(where + ": its \"manifest\" is not this publisher's");
            }
            else if (m->revoked() || !m->signingKey)
            {
                fail(where + ": its \"manifest\" revokes the publisher's master key");
            }
            else
            {
                own = std::move(m);
            }
        }
        Manifest const& effective = own ? *own : *manifest;
        if (held == nullptr || effective.sequence > held->sequence)
        {
            if (own)
            {
                heldOwn = std::move(own);
                held = &*heldOwn;
            }
            else
            {
                held = &*manifest;
            }
        }

        auto const sig = strUnHex(entry.signature);
        auto const data = base64Decode(entry.blob);
        if (!sig || !verify(*held->signingKey, makeSlice(data), makeSlice(*sig)))
            fail(where + ": the signature does not verify under the signing key");

        std::optional<UnsignedList> parsed;
        try
        {
            parsed = checkedList(data, parseObject(data));
        }
        catch (std::runtime_error const& e)
        {
            fail(where + ": " + e.what());
        }

        if (parsed)
        {
            found[jss::sequence] = parsed->sequence;
            if (parsed->effective)
                found[jss::effective] = *parsed->effective;
            found[jss::expiration] = parsed->expiration;
            found[jss::validators] = json::UInt(parsed->validators.size());
            found["expired"] = parsed->expiration <= now;
            found["pending"] = parsed->effective && *parsed->effective > now;

            if (parsed->expiration <= now)
                fail(where + ": expired");

            if (previous && parsed->sequence <= previous->first)
            {
                fail(
                    where + ": \"sequence\" is not above blob " + std::to_string(previous->second) +
                    "'s");
            }
            previous.emplace(parsed->sequence, current);

            if (want)
            {
                std::set<PublicKey> const have(
                    parsed->validators.begin(), parsed->validators.end());
                if (have != *want)
                    fail(where + ": the validators differ from the expected list");
            }
        }

        report["blobs"].append(found);
    }

    report["signing_key"] = strHex(*held->signingKey);
    report["manifest_sequence"] = held->sequence;
    return report;
}

}  // namespace xrpl
