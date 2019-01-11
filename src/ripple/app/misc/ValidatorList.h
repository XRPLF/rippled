//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_MISC_VALIDATORLIST_H_INCLUDED
#define RIPPLE_APP_MISC_VALIDATORLIST_H_INCLUDED

#include <ripple/app/misc/Manifest.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/core/TimeKeeper.h>
#include <ripple/crypto/csprng.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/PublicKey.h>
#include <boost/iterator/counting_iterator.hpp>
#include <boost/range/adaptors.hpp>
#include <mutex>
#include <shared_mutex>
#include <numeric>

namespace ripple {

enum class ListDisposition
{
    /// List is valid
    accepted = 0,

    /// Same sequence as current list
    same_sequence,

    /// List version is not supported
    unsupported_version,

    /// List signed by untrusted publisher key
    untrusted,

    /// Trusted publisher key, but seq is too old
    stale,

    /// Invalid format or signature
    invalid
};

std::string
to_string(ListDisposition disposition);

/** Changes in trusted nodes after updating validator list
*/
struct TrustChanges
{
    explicit TrustChanges() = default;

    hash_set<NodeID> added;
    hash_set<NodeID> removed;
};

/**
    Trusted Validators List
    -----------------------

    Rippled accepts ledger proposals and validations from trusted validator
    nodes. A ledger is considered fully-validated once the number of received
    trusted validations for a ledger meets or exceeds a quorum value.

    This class manages the set of validation public keys the local rippled node
    trusts. The list of trusted keys is populated using the keys listed in the
    configuration file as well as lists signed by trusted publishers. The
    trusted publisher public keys are specified in the config.

    New lists are expected to include the following data:

    @li @c "blob": Base64-encoded JSON string containing a @c "sequence", @c
        "expiration", and @c "validators" field. @c "expiration" contains the
        Ripple timestamp (seconds since January 1st, 2000 (00:00 UTC)) for when
        the list expires. @c "validators" contains an array of objects with a
        @c "validation_public_key" and optional @c "manifest" field.
        @c "validation_public_key" should be the hex-encoded master public key.
        @c "manifest" should be the base64-encoded validator manifest.

    @li @c "manifest": Base64-encoded serialization of a manifest containing the
        publisher's master and signing public keys.

    @li @c "signature": Hex-encoded signature of the blob using the publisher's
        signing key.

    @li @c "version": 1

    Individual validator lists are stored separately by publisher. The number of
    lists on which a validator's public key appears is also tracked.

    The list of trusted validation public keys is reset at the start of each
    consensus round to take into account the latest known lists as well as the
    set of validators from whom validations are being received. Listed
    validation public keys are shuffled and then sorted by the number of lists
    they appear on. (The shuffling makes the order/rank of validators with the
    same number of listings non-deterministic.) A quorum value is calculated for
    the new trusted validator list. If there is only one list, all listed keys
    are trusted. Otherwise, the trusted list size is set to 125% of the quorum.
*/
class ValidatorList
{
    struct PublisherList
    {
        explicit PublisherList() = default;

        bool available;
        std::vector<PublicKey> list;
        std::size_t sequence;
        TimeKeeper::time_point expiration;
        std::string siteUri;
    };

    ManifestCache& validatorManifests_;
    ManifestCache& publisherManifests_;
    TimeKeeper& timeKeeper_;
    beast::Journal j_;
    std::shared_timed_mutex mutable mutex_;

    std::atomic<std::size_t> quorum_;
    boost::optional<std::size_t> minimumQuorum_;

    // Published lists stored by publisher master public key
    hash_map<PublicKey, PublisherList> publisherLists_;

    // Listed master public keys with the number of lists they appear on
    hash_map<PublicKey, std::size_t> keyListings_;

    // The current list of trusted master keys
    hash_set<PublicKey> trustedKeys_;

    PublicKey localPubKey_;

    // Currently supported version of publisher list format
    static constexpr std::uint32_t requiredListVersion = 1;

public:
    ValidatorList (
        ManifestCache& validatorManifests,
        ManifestCache& publisherManifests,
        TimeKeeper& timeKeeper,
        beast::Journal j,
        boost::optional<std::size_t> minimumQuorum = boost::none);
    ~ValidatorList () = default;

    /** Load configured trusted keys.

        @param localSigningKey This node's validation public key

        @param configKeys List of trusted keys from config. Each entry consists
        of a base58 encoded validation public key, optionally followed by a
        comment.

        @param publisherKeys List of trusted publisher public keys. Each entry
        contains a base58 encoded account public key.

        @par Thread Safety

        May be called concurrently

        @return `false` if an entry is invalid or unparsable
    */
    bool
    load (
        PublicKey const& localSigningKey,
        std::vector<std::string> const& configKeys,
        std::vector<std::string> const& publisherKeys);

    /** Apply published list of public keys

        @param manifest base64-encoded publisher key manifest

        @param blob base64-encoded json containing published validator list

        @param signature Signature of the decoded blob

        @param version Version of published list format

        @return `ListDisposition::accepted` if list was successfully applied

        @par Thread Safety

        May be called concurrently
    */
    ListDisposition
    applyList (
        std::string const& manifest,
        std::string const& blob,
        std::string const& signature,
        std::uint32_t version,
        std::string siteUri);

    /** Update trusted nodes

        Reset the trusted nodes based on latest manifests, received validations,
        and lists.

        @param seenValidators Set of NodeIDs of validators that have signed
        recently received validations

        @return TrustedKeyChanges instance with newly trusted or untrusted
        node identities.

        @par Thread Safety

        May be called concurrently
    */
    TrustChanges
    updateTrusted (hash_set<NodeID> const& seenValidators);

    /** Get quorum value for current trusted key set

        The quorum is the minimum number of validations needed for a ledger to
        be fully validated. It can change when the set of trusted validation
        keys is updated (at the start of each consensus round) and primarily
        depends on the number of trusted keys.

        @par Thread Safety

        May be called concurrently

        @return quorum value
    */
    std::size_t
    quorum () const
    {
        return quorum_;
    }

    /** Returns `true` if public key is trusted

        @param identity Validation public key

        @par Thread Safety

        May be called concurrently
    */
    bool
    trusted (
        PublicKey const& identity) const;

    /** Returns `true` if public key is included on any lists

        @param identity Validation public key

        @par Thread Safety

        May be called concurrently
    */
    bool
    listed (
        PublicKey const& identity) const;

    /** Returns master public key if public key is trusted

        @param identity Validation public key

        @return `boost::none` if key is not trusted

        @par Thread Safety

        May be called concurrently
    */
    boost::optional<PublicKey>
    getTrustedKey (
        PublicKey const& identity) const;

    /** Returns listed master public if public key is included on any lists

        @param identity Validation public key

        @return `boost::none` if key is not listed

        @par Thread Safety

        May be called concurrently
    */
    boost::optional<PublicKey>
    getListedKey (
        PublicKey const& identity) const;

    /** Returns `true` if public key is a trusted publisher

        @param identity Publisher public key

        @par Thread Safety

        May be called concurrently
    */
    bool
    trustedPublisher (
        PublicKey const& identity) const;

    /** Returns local validator public key

        @par Thread Safety

        May be called concurrently
    */
    PublicKey
    localPublicKey () const;

    /** Invokes the callback once for every listed validation public key.

        @note Undefined behavior results when calling ValidatorList members from
        within the callback

        The arguments passed into the lambda are:

        @li The validation public key

        @li A boolean indicating whether this is a trusted key

        @par Thread Safety

        May be called concurrently
    */
    void
    for_each_listed (
        std::function<void(PublicKey const&, bool)> func) const;

    /** Return the number of configured validator list sites. */
    std::size_t
    count() const;

    /** Return the time when the validator list will expire

        @note This may be a time in the past if a published list has not
        been updated since its expiration. It will be boost::none if any
        configured published list has not been fetched.

        @par Thread Safety
        May be called concurrently
    */
    boost::optional<TimeKeeper::time_point>
    expires() const;

    /** Return a JSON representation of the state of the validator list

        @par Thread Safety
        May be called concurrently
    */
    Json::Value
    getJson() const;

private:
    /** Check response for trusted valid published list

        @return `ListDisposition::accepted` if list can be applied

        @par Thread Safety

        Calling public member function is expected to lock mutex
    */
    ListDisposition
    verify (
        Json::Value& list,
        PublicKey& pubKey,
        std::string const& manifest,
        std::string const& blob,
        std::string const& signature);

    /** Stop trusting publisher's list of keys.

        @param publisherKey Publisher public key

        @return `false` if key was not trusted

        @par Thread Safety

        Calling public member function is expected to lock mutex
    */
    bool
    removePublisherList (PublicKey const& publisherKey);

    /** Return quorum for trusted validator set

        @param trusted Number of trusted validator keys

        @param seen Number of trusted validators that have signed
        recently received validations */
    std::size_t
    calculateQuorum (
        std::size_t trusted, std::size_t seen);
};
} // ripple

#endif
