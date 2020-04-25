//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_MISC_MANIFEST_H_INCLUDED
#define RIPPLE_APP_MISC_MANIFEST_H_INCLUDED

#include <ripple/basics/UnorderedContainers.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <boost/optional.hpp>
#include <string>

namespace ripple {

/*
    Validator key manifests
    -----------------------

    Suppose the secret keys installed on a Ripple validator are compromised. Not
    only do you have to generate and install new key pairs on each validator,
    EVERY rippled needs to have its config updated with the new public keys, and
    is vulnerable to forged validation signatures until this is done.  The
    solution is a new layer of indirection: A master secret key under
    restrictive access control is used to sign a "manifest": essentially, a
    certificate including the master public key, an ephemeral public key for
    verifying validations (which will be signed by its secret counterpart), a
    sequence number, and a digital signature.

    The manifest has two serialized forms: one which includes the digital
    signature and one which doesn't.  There is an obvious causal dependency
    relationship between the (latter) form with no signature, the signature
    of that form, and the (former) form which includes that signature.  In
    other words, a message can't contain a signature of itself.  The code
    below stores a serialized manifest which includes the signature, and
    dynamically generates the signatureless form when it needs to verify
    the signature.

    An instance of ManifestCache stores, for each trusted validator, (a) its
    master public key, and (b) the most senior of all valid manifests it has
    seen for that validator, if any.  On startup, the [validator_token] config
    entry (which contains the manifest for this validator) is decoded and
    added to the manifest cache.  Other manifests are added as "gossip"
    received from rippled peers.

    When an ephemeral key is compromised, a new signing key pair is created,
    along with a new manifest vouching for it (with a higher sequence number),
    signed by the master key.  When a rippled peer receives the new manifest,
    it verifies it with the master key and (assuming it's valid) discards the
    old ephemeral key and stores the new one.  If the master key itself gets
    compromised, a manifest with sequence number 0xFFFFFFFF will supersede a
    prior manifest and discard any existing ephemeral key without storing a
    new one.  These revocation manifests are loaded from the
    [validator_key_revocation] config entry as well as received as gossip from
    peers.  Since no further manifests for this master key will be accepted
    (since no higher sequence number is possible), and no signing key is on
    record, no validations will be accepted from the compromised validator.
*/

//------------------------------------------------------------------------------

struct Manifest
{
    /// The manifest in serialized form.
    std::string serialized;

    /// The master key associated with this manifest.
    PublicKey masterKey;

    /// The ephemeral key associated with this manifest.
    PublicKey signingKey;

    /// The sequence number of this manifest.
    std::uint32_t sequence = 0;

    /// The domain, if one was specified in the manifest; empty otherwise.
    std::string domain;

    Manifest() = default;
    Manifest(Manifest const& other) = delete;
    Manifest&
    operator=(Manifest const& other) = delete;
    Manifest(Manifest&& other) = default;
    Manifest&
    operator=(Manifest&& other) = default;

    /// Returns `true` if manifest signature is valid
    bool
    verify() const;

    /// Returns hash of serialized manifest data
    uint256
    hash() const;

    /// Returns `true` if manifest revokes master key
    bool
    revoked() const;

    /// Returns manifest signature
    boost::optional<Blob>
    getSignature() const;

    /// Returns manifest master key signature
    Blob
    getMasterSignature() const;
};

/** Constructs Manifest from serialized string

    @param s Serialized manifest string

    @return `boost::none` if string is invalid

    @note This does not verify manifest signatures.
          `Manifest::verify` should be called after constructing manifest.
*/
/** @{ */
boost::optional<Manifest>
deserializeManifest(Slice s);

inline boost::optional<Manifest>
deserializeManifest(std::string const& s)
{
    return deserializeManifest(makeSlice(s));
}

template <
    class T,
    class = std::enable_if_t<
        std::is_same<T, char>::value || std::is_same<T, unsigned char>::value>>
boost::optional<Manifest>
deserializeManifest(std::vector<T> const& v)
{
    return deserializeManifest(makeSlice(v));
}
/** @} */

inline bool
operator==(Manifest const& lhs, Manifest const& rhs)
{
    // In theory, comparing the two serialized strings should be
    // sufficient.
    return lhs.sequence == rhs.sequence && lhs.masterKey == rhs.masterKey &&
        lhs.signingKey == rhs.signingKey && lhs.domain == rhs.domain &&
        lhs.serialized == rhs.serialized;
}

inline bool
operator!=(Manifest const& lhs, Manifest const& rhs)
{
    return !(lhs == rhs);
}

struct ValidatorToken
{
    std::string manifest;
    SecretKey validationSecret;
};

boost::optional<ValidatorToken>
loadValidatorToken(std::vector<std::string> const& blob);

enum class ManifestDisposition {
    /// Manifest is valid
    accepted = 0,

    /// Sequence is too old
    stale,

    /// Timely, but invalid signature
    invalid
};

inline std::string
to_string(ManifestDisposition m)
{
    switch (m)
    {
        case ManifestDisposition::accepted:
            return "accepted";
        case ManifestDisposition::stale:
            return "stale";
        case ManifestDisposition::invalid:
            return "invalid";
        default:
            return "unknown";
    }
}

class DatabaseCon;

/** Remembers manifests with the highest sequence number. */
class ManifestCache
{
private:
    beast::Journal mutable j_;
    std::mutex apply_mutex_;
    std::mutex mutable read_mutex_;

    /** Active manifests stored by master public key. */
    hash_map<PublicKey, Manifest> map_;

    /** Master public keys stored by current ephemeral public key. */
    hash_map<PublicKey, PublicKey> signingToMasterKeys_;

public:
    explicit ManifestCache(
        beast::Journal j = beast::Journal(beast::Journal::getNullSink()))
        : j_(j)
    {
    }

    /** Returns master key's current signing key.

        @param pk Master public key

        @return pk if no known signing key from a manifest

        @par Thread Safety

        May be called concurrently
    */
    PublicKey
    getSigningKey(PublicKey const& pk) const;

    /** Returns ephemeral signing key's master public key.

        @param pk Ephemeral signing public key

        @return pk if signing key is not in a valid manifest

        @par Thread Safety

        May be called concurrently
    */
    PublicKey
    getMasterKey(PublicKey const& pk) const;

    /** Returns master key's current manifest sequence.

        @return sequence corresponding to Master public key
          if configured or boost::none otherwise
    */
    boost::optional<std::uint32_t>
    getSequence(PublicKey const& pk) const;

    /** Returns domain claimed by a given public key

        @return domain corresponding to Master public key
          if present, otherwise boost::none
    */
    boost::optional<std::string>
    getDomain(PublicKey const& pk) const;

    /** Returns mainfest corresponding to a given public key

        @return manifest corresponding to Master public key
          if present, otherwise boost::none
    */
    boost::optional<std::string>
    getManifest(PublicKey const& pk) const;

    /** Returns `true` if master key has been revoked in a manifest.

        @param pk Master public key

        @par Thread Safety

        May be called concurrently
    */
    bool
    revoked(PublicKey const& pk) const;

    /** Add manifest to cache.

        @param m Manifest to add

        @return `ManifestDisposition::accepted` if successful, or
                `stale` or `invalid` otherwise

        @par Thread Safety

        May be called concurrently
    */
    ManifestDisposition
    applyManifest(Manifest m);

    /** Populate manifest cache with manifests in database and config.

        @param dbCon Database connection with dbTable

        @param dbTable Database table

        @param configManifest Base64 encoded manifest for local node's
            validator keys

        @param configRevocation Base64 encoded validator key revocation
            from the config

        @par Thread Safety

        May be called concurrently
    */
    bool
    load(
        DatabaseCon& dbCon,
        std::string const& dbTable,
        std::string const& configManifest,
        std::vector<std::string> const& configRevocation);

    /** Populate manifest cache with manifests in database.

        @param dbCon Database connection with dbTable

        @param dbTable Database table

        @par Thread Safety

        May be called concurrently
    */
    void
    load(DatabaseCon& dbCon, std::string const& dbTable);

    /** Save cached manifests to database.

        @param dbCon Database connection with `ValidatorManifests` table

        @param isTrusted Function that returns true if manifest is trusted

        @par Thread Safety

        May be called concurrently
    */
    void
    save(
        DatabaseCon& dbCon,
        std::string const& dbTable,
        std::function<bool(PublicKey const&)> isTrusted);

    /** Invokes the callback once for every populated manifest.

        @note Undefined behavior results when calling ManifestCache members from
        within the callback

        @param f Function called for each manifest

        @par Thread Safety

        May be called concurrently
    */
    template <class Function>
    void
    for_each_manifest(Function&& f) const
    {
        std::lock_guard lock{read_mutex_};
        for (auto const& [_, manifest] : map_)
        {
            (void)_;
            f(manifest);
        }
    }

    /** Invokes the callback once for every populated manifest.

        @note Undefined behavior results when calling ManifestCache members from
        within the callback

        @param pf Pre-function called with the maximum number of times f will be
            called (useful for memory allocations)

        @param f Function called for each manifest

        @par Thread Safety

        May be called concurrently
    */
    template <class PreFun, class EachFun>
    void
    for_each_manifest(PreFun&& pf, EachFun&& f) const
    {
        std::lock_guard lock{read_mutex_};
        pf(map_.size());
        for (auto const& [_, manifest] : map_)
        {
            (void)_;
            f(manifest);
        }
    }
};

}  // namespace ripple

#endif
