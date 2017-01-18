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
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/beast/utility/Journal.h>
#include <boost/optional.hpp>
#include <string>

namespace ripple {

/*
    Validator key manifests
    -----------------------

    Suppose the secret keys installed on a Ripple validator are compromised.  Not
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

    There are two stores of information within rippled related to manifests.
    An instance of ManifestCache stores, for each trusted validator, (a) its
    master public key, and (b) the most senior of all valid manifests it has
    seen for that validator, if any.  On startup, the [validator_token] config
    entry (which contains the manifest for this validator) is decoded and
    added to the manifest cache.  Other manifests are added as "gossip" is
    received from rippled peers.

    The other data store (which does not involve manifests per se) contains
    the set of active ephemeral validator keys.  Keys are added to the set
    when a manifest is accepted, and removed when that manifest is obsoleted.

    When an ephemeral key is compromised, a new signing key pair is created,
    along with a new manifest vouching for it (with a higher sequence number),
    signed by the master key.  When a rippled peer receives the new manifest,
    it verifies it with the master key and (assuming it's valid) discards the
    old ephemeral key and stores the new one.  If the master key itself gets
    compromised, a manifest with sequence number 0xFFFFFFFF will supersede a
    prior manifest and discard any existing ephemeral key without storing a
    new one.  Since no further manifests for this master key will be accepted
    (since no higher sequence number is possible), and no signing key is on
    record, no validations will be accepted from the compromised validator.
*/

//------------------------------------------------------------------------------

struct Manifest
{
    std::string serialized;
    PublicKey masterKey;
    PublicKey signingKey;
    std::uint32_t sequence;

    Manifest(std::string s, PublicKey pk, PublicKey spk, std::uint32_t seq);

    Manifest(Manifest&& other) = default;
    Manifest& operator=(Manifest&& other) = default;

    inline bool
    operator==(Manifest const& rhs) const
    {
        return sequence == rhs.sequence && masterKey == rhs.masterKey &&
               signingKey == rhs.signingKey && serialized == rhs.serialized;
    }

    inline bool
    operator!=(Manifest const& rhs) const
    {
        return !(*this == rhs);
    }

    /** Constructs Manifest from serialized string

        @param s Serialized manifest string

        @return `boost::none` if string is invalid
    */
    static boost::optional<Manifest> make_Manifest(std::string s);

    /// Returns `true` if manifest signature is valid
    bool verify () const;

    /// Returns hash of serialized manifest data
    uint256 hash () const;

    /// Returns `true` if manifest revokes master key
    bool revoked () const;

    /// Returns manifest signature
    Blob getSignature () const;

    /// Returns manifest master key signature
    Blob getMasterSignature () const;
};

struct ValidatorToken
{
    std::string manifest;
    SecretKey validationSecret;

private:
    ValidatorToken(std::string const& m, SecretKey const& valSecret);

public:
    ValidatorToken(ValidatorToken const&) = delete;
    ValidatorToken(ValidatorToken&& other) = default;

    static boost::optional<ValidatorToken>
    make_ValidatorToken(std::vector<std::string> const& tokenBlob);
};

enum class ManifestDisposition
{
    /// Manifest is valid
    accepted = 0,

    /// Sequence is too old
    stale,

    /// Timely, but invalid signature
    invalid
};

class DatabaseCon;

/** Remembers manifests with the highest sequence number. */
class ManifestCache
{
private:
    beast::Journal mutable j_;
    std::mutex apply_mutex_;
    std::mutex mutable read_mutex_;

    /** Active manifests stored by master public key. */
    hash_map <PublicKey, Manifest> map_;

    /** Master public keys stored by current ephemeral public key. */
    hash_map <PublicKey, PublicKey> signingToMasterKeys_;

public:
    explicit
    ManifestCache (beast::Journal j = beast::Journal())
        : j_ (j)
    {
    };

    /** Returns master key's current signing key.

        @param pk Master public key

        @return pk if no known signing key from a manifest

        @par Thread Safety

        May be called concurrently
    */
    PublicKey
    getSigningKey (PublicKey const& pk) const;

    /** Returns ephemeral signing key's master public key.

        @param pk Ephemeral signing public key

        @return pk if signing key is not in a valid manifest

        @par Thread Safety

        May be called concurrently
    */
    PublicKey
    getMasterKey (PublicKey const& pk) const;

    /** Returns `true` if master key has been revoked in a manifest.

        @param pk Master public key

        @par Thread Safety

        May be called concurrently
    */
    bool
    revoked (PublicKey const& pk) const;

    /** Add manifest to cache.

        @param m Manifest to add

        @return `ManifestDisposition::accepted` if successful, or
                `stale` or `invalid` otherwise

        @par Thread Safety

        May be called concurrently
    */
    ManifestDisposition
    applyManifest (
        Manifest m);

    /** Populate manifest cache with manifests in database and config.

        @param dbCon Database connection with dbTable

        @param dbTable Database table

        @param configManifest Base64 encoded manifest for local node's
            validator keys

        @par Thread Safety

        May be called concurrently
    */
    bool load (
        DatabaseCon& dbCon, std::string const& dbTable,
        std::string const& configManifest);

    /** Populate manifest cache with manifests in database.

        @param dbCon Database connection with dbTable

        @param dbTable Database table

        @par Thread Safety

        May be called concurrently
    */
    void load (
        DatabaseCon& dbCon, std::string const& dbTable);

    /** Save cached manifests to database.

        @param dbCon Database connection with `ValidatorManifests` table

        @param isTrusted Function that returns true if manifest is trusted

        @par Thread Safety

        May be called concurrently
    */
    void save (
        DatabaseCon& dbCon, std::string const& dbTable,
        std::function <bool (PublicKey const&)> isTrusted);

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
        std::lock_guard<std::mutex> lock{read_mutex_};
        for (auto const& m : map_)
        {
            f(m.second);
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
        std::lock_guard<std::mutex> lock{read_mutex_};
        pf(map_.size ());
        for (auto const& m : map_)
        {
            f(m.second);
        }
    }
};

} // ripple

#endif
