#pragma once

#include <xrpl/json/json_value.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/server/Manifest.h>

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
 * Reads a token from a file holding the [validator_token] block as
 * `create_token` prints it. The section header and `#` comment lines are
 * ignored and the base64 lines are joined.
 *
 * @throws std::runtime_error if the file cannot be read or is not a token
 */
ValidatorToken
loadTokenFile(boost::filesystem::path const& tokenFile);

/**
 * Reads a base64 manifest from a file. Comment lines and line breaks are
 * ignored.
 *
 * @throws std::runtime_error if the file cannot be read or the manifest does
 *         not deserialize and verify
 */
Manifest
loadManifestFile(boost::filesystem::path const& manifestFile);

/**
 * A validator list before it is signed: the canonical bytes the signing key
 * signs, and the fields checked before signing.
 */
struct UnsignedList
{
    // Canonical JSON text: compact, `, ` and `: ` separators, key order as
    // written.
    std::string canonical;
    std::uint32_t sequence = 0;
    std::optional<std::uint32_t> effective;
    std::uint32_t expiration = 0;
    // Master keys of the listed validators.
    std::vector<PublicKey> validators;
};

/**
 * Returns the canonical form of a JSON document: whitespace outside strings
 * removed, one space after each `,` and `:`, key order preserved.
 *
 * @throws std::runtime_error if the text is not a JSON document
 */
std::string
canonicalJson(std::string const& text);

/**
 * Parses an unsigned list and checks its fields: `sequence` and `expiration`
 * are unsigned integers, `effective` if present is earlier than `expiration`,
 * and every entry of `validators` has a `validation_public_key` that is a hex
 * public key and, if present, a `manifest` for that key.
 *
 * @throws std::runtime_error naming the first failed check
 */
UnsignedList
parseUnsignedList(std::string const& text);

/**
 * Reads and parses an unsigned list file.
 *
 * @throws std::runtime_error if the file cannot be read or fails a check
 */
UnsignedList
loadUnsignedList(boost::filesystem::path const& file);

/**
 * Returns the hex signature of the list's canonical bytes.
 */
std::string
signList(UnsignedList const& list, PublicKey const& signingKey, SecretKey const& signingSecret);

/**
 * Builds the document a publisher serves.
 *
 * Version 1 is `{blob, manifest, public_key, signature, version}`. Version 2
 * carries the blob and signature inside `blobs_v2`; when @p append is given it
 * must be a version 2 document for the same master key and the new blob is
 * added to it.
 *
 * @throws std::runtime_error if @p append is not a version 2 document for
 *         @p masterKey or already holds the maximum number of blobs
 */
json::Value
makeSignedList(
    std::string const& manifestBase64,
    PublicKey const& masterKey,
    UnsignedList const& list,
    std::string const& signatureHex,
    unsigned version,
    std::optional<json::Value> const& append);

/**
 * Seconds since the XRP Ledger epoch, now.
 */
std::uint32_t
rippleEpochNow();

/**
 * The outcome of checking a published list.
 */
struct ListVerification
{
    bool ok = true;
    std::vector<std::string> errors;
    // What was found: master key, signing key, manifest sequence, version,
    // and one entry per blob.
    json::Value report;
};

/**
 * Checks a published list the way a server does before trusting it: the
 * manifest verifies and names the `public_key`, every blob's signature
 * verifies under the manifest's signing key, every blob parses, and none has
 * expired at @p now. Every listed validator with a manifest must own it.
 *
 * @param list The document as served
 * @param expectedRoster When set, every blob must list exactly these master
 *                       keys
 * @param expectedKey When set, the manifest's master key must be this key
 * @param now Seconds since the XRP Ledger epoch
 */
ListVerification
verifyList(
    json::Value const& list,
    std::optional<UnsignedList> const& expectedRoster,
    std::optional<PublicKey> const& expectedKey,
    std::uint32_t now);

}  // namespace xrpl
