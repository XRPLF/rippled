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

#ifndef RIPPLE_OVERLAY_MANIFEST_H_INCLUDED
#define RIPPLE_OVERLAY_MANIFEST_H_INCLUDED

#include <ripple/app/misc/UniqueNodeList.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STExchange.h>
#include <beast/utility/Journal.h>
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
    seen for that validator, if any.  On startup, the [validator_keys] config
    entries are used to prime the manifest cache with the trusted master keys.
    At this point, the manifest cache has all the entries it will ever have,
    but none of them have manifests.  The [validation_manifest] config entry
    (which is the manifest for this validator) is then decoded and added to
    the manifest cache.  Other manifests are added as "gossip" is received
    from rippled peers.

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

#ifdef _MSC_VER
    Manifest(Manifest&& other)
      : serialized(std::move(other.serialized))
      , masterKey(std::move(other.masterKey))
      , signingKey(std::move(other.signingKey))
      , sequence(other.sequence)
    {
    }

    Manifest& operator=(Manifest&& other)
    {
        serialized = std::move(other.serialized);
        masterKey = std::move(other.masterKey);
        signingKey = std::move(other.signingKey);
        sequence = other.sequence;
        return *this;
    }
#else
    Manifest(Manifest&& other) = default;
    Manifest& operator=(Manifest&& other) = default;
#endif

    bool verify () const;
    uint256 hash () const;
    bool revoked () const;
};

boost::optional<Manifest> make_Manifest(std::string s);

inline bool operator==(Manifest const& lhs, Manifest const& rhs)
{
    return lhs.serialized == rhs.serialized && lhs.masterKey == rhs.masterKey &&
           lhs.signingKey == rhs.signingKey && lhs.sequence == rhs.sequence;
}

inline bool operator!=(Manifest const& lhs, Manifest const& rhs)
{
    return !(lhs == rhs);
}

enum class ManifestDisposition
{
    accepted = 0, // everything checked out

    untrusted,    // manifest declares a master key we don't trust
    stale,        // trusted master key, but seq is too old
    invalid,      // trusted and timely, but invalid signature
};

class DatabaseCon;
/** Remembers manifests with the highest sequence number. */
class ManifestCache
{
private:
    struct MappedType
    {
        MappedType() = default;
#ifdef _MSC_VER
        MappedType(MappedType&& rhs)
            :comment (std::move (rhs.comment))
            , m (std::move (rhs.m))
        {
        }
        MappedType& operator=(MappedType&& rhs)
        {
            comment = std::move (rhs.comment);
            m = std::move (rhs.m);
            return *this;
        }
#else
        MappedType(MappedType&&) = default;
        MappedType& operator=(MappedType&&) = default;
#endif
        MappedType(std::string comment,
                   std::string serialized,
                   PublicKey pk, PublicKey spk, std::uint32_t seq)
            :comment (std::move(comment))
        {
            m.emplace (std::move(serialized), std::move(pk), std::move(spk),
                       seq);
        }

        std::string comment;
        boost::optional<Manifest> m;
    };

    using MapType = hash_map<PublicKey, MappedType>;

    mutable std::mutex mutex_;
    MapType map_;

    ManifestDisposition
    canApply (PublicKey const& pk, std::uint32_t seq,
        beast::Journal const& journal) const;

public:
    ManifestCache() = default;
    ManifestCache (ManifestCache const&) = delete;
    ManifestCache& operator= (ManifestCache const&) = delete;
    ~ManifestCache() = default;

    void configValidatorKey(std::string const& line, beast::Journal const& journal);
    void configManifest (Manifest m, UniqueNodeList& unl, beast::Journal const& journal);

    void addTrustedKey (PublicKey const& pk, std::string comment);

    ManifestDisposition
    applyManifest (
        Manifest m, UniqueNodeList& unl, beast::Journal const& journal);

    void load (
        DatabaseCon& dbCon, UniqueNodeList& unl, beast::Journal const& journal);
    void save (DatabaseCon& dbCon) const;

    // A "for_each" for populated manifests only
    template <class Function>
    void
    for_each_manifest(Function&& f) const
    {
        std::lock_guard<std::mutex> lock (mutex_);
        for (auto const& e : map_)
        {
            if (auto const& m = e.second.m)
                f(*m);
        }
    }

    // A "for_each" for populated manifests only
    // The PreFun is called with the maximum number of
    // times EachFun will be called (useful for memory allocations)
    template <class PreFun, class EachFun>
    void
    for_each_manifest(PreFun&& pf, EachFun&& f) const
    {
        std::lock_guard<std::mutex> lock (mutex_);
        pf(map_.size ());
        for (auto const& e : map_)
        {
            if (auto const& m = e.second.m)
                f(*m);
        }
    }
};

} // ripple

#endif
