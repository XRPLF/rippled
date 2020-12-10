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
#include <boost/thread/shared_mutex.hpp>
#include <mutex>
#include <numeric>
#include <shared_mutex>

namespace ripple {

// predeclaration
class Overlay;
class HashRouter;
class STValidation;

enum class ListDisposition {
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
        std::string rawManifest;
        std::string rawBlob;
        std::string rawSignature;
        std::uint32_t rawVersion;
        uint256 hash;
    };

    ManifestCache& validatorManifests_;
    ManifestCache& publisherManifests_;
    TimeKeeper& timeKeeper_;
    boost::filesystem::path const dataPath_;
    beast::Journal const j_;
    boost::shared_mutex mutable mutex_;
    using unique_lock = std::unique_lock<boost::shared_mutex>;
    using shared_lock = std::shared_lock<boost::shared_mutex>;

    std::atomic<std::size_t> quorum_;
    boost::optional<std::size_t> minimumQuorum_;

    // Published lists stored by publisher master public key
    hash_map<PublicKey, PublisherList> publisherLists_;

    // Listed master public keys with the number of lists they appear on
    hash_map<PublicKey, std::size_t> keyListings_;

    // The current list of trusted master keys
    hash_set<PublicKey> trustedMasterKeys_;

    // The current list of trusted signing keys. For those validators using
    // a manifest, the signing key is the ephemeral key. For the ones using
    // a seed, the signing key is the same as the master key.
    hash_set<PublicKey> trustedSigningKeys_;

    PublicKey localPubKey_;

    // The master public keys of the current negative UNL
    hash_set<PublicKey> negativeUNL_;

    // Currently supported version of publisher list format
    static constexpr std::uint32_t requiredListVersion = 1;
    static const std::string filePrefix_;

public:
    ValidatorList(
        ManifestCache& validatorManifests,
        ManifestCache& publisherManifests,
        TimeKeeper& timeKeeper,
        std::string const& databasePath,
        beast::Journal j,
        boost::optional<std::size_t> minimumQuorum = boost::none);
    ~ValidatorList() = default;

    /** Describes the result of processing a Validator List (UNL),
    including some of the information from the list which can
    be used by the caller to know which list publisher is
    involved.
    */
    struct PublisherListStats
    {
        explicit PublisherListStats(ListDisposition d) : disposition(d)
        {
        }

        PublisherListStats(
            ListDisposition d,
            PublicKey key,
            bool avail,
            std::size_t seq)
            : disposition(d), publisherKey(key), available(avail), sequence(seq)
        {
        }

        ListDisposition disposition;
        boost::optional<PublicKey> publisherKey;
        bool available = false;
        boost::optional<std::size_t> sequence;
    };

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
    load(
        PublicKey const& localSigningKey,
        std::vector<std::string> const& configKeys,
        std::vector<std::string> const& publisherKeys);

    /** Apply published list of public keys, then broadcast it to all
        peers that have not seen it or sent it.

        @param manifest base64-encoded publisher key manifest

        @param blob base64-encoded json containing published validator list

        @param signature Signature of the decoded blob

        @param version Version of published list format

        @param siteUri Uri of the site from which the list was validated

        @param hash Hash of the data parameters

        @param overlay Overlay object which will handle sending the message

        @param hashRouter HashRouter object which will determine which
            peers not to send to

        @return `ListDisposition::accepted`, plus some of the publisher
            information, if list was successfully applied

        @par Thread Safety

        May be called concurrently
    */
    PublisherListStats
    applyListAndBroadcast(
        std::string const& manifest,
        std::string const& blob,
        std::string const& signature,
        std::uint32_t version,
        std::string siteUri,
        uint256 const& hash,
        Overlay& overlay,
        HashRouter& hashRouter);

    /** Apply published list of public keys

        @param manifest base64-encoded publisher key manifest

        @param blob base64-encoded json containing published validator list

        @param signature Signature of the decoded blob

        @param version Version of published list format

        @param siteUri Uri of the site from which the list was validated

        @param hash Optional hash of the data parameters.
            Defaults to uninitialized

        @return `ListDisposition::accepted`, plus some of the publisher
            information, if list was successfully applied

        @par Thread Safety

        May be called concurrently
    */
    PublisherListStats
    applyList(
        std::string const& manifest,
        std::string const& blob,
        std::string const& signature,
        std::uint32_t version,
        std::string siteUri,
        boost::optional<uint256> const& hash = {});

    /* Attempt to read previously stored list files. Expected to only be
       called when loading from URL fails.

       @return A list of valid file:// URLs, if any.

       @par Thread Safety

       May be called concurrently
    */
    std::vector<std::string>
    loadLists();

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
    updateTrusted(hash_set<NodeID> const& seenValidators);

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
    quorum() const
    {
        return quorum_;
    }

    /** Returns `true` if public key is trusted

        @param identity Validation public key

        @par Thread Safety

        May be called concurrently
    */
    bool
    trusted(PublicKey const& identity) const;

    /** Returns `true` if public key is included on any lists

        @param identity Validation public key

        @par Thread Safety

        May be called concurrently
    */
    bool
    listed(PublicKey const& identity) const;

    /** Returns master public key if public key is trusted

        @param identity Validation public key

        @return `boost::none` if key is not trusted

        @par Thread Safety

        May be called concurrently
    */
    boost::optional<PublicKey>
    getTrustedKey(PublicKey const& identity) const;

    /** Returns listed master public if public key is included on any lists

        @param identity Validation public key

        @return `boost::none` if key is not listed

        @par Thread Safety

        May be called concurrently
    */
    boost::optional<PublicKey>
    getListedKey(PublicKey const& identity) const;

    /** Returns `true` if public key is a trusted publisher

        @param identity Publisher public key

        @par Thread Safety

        May be called concurrently
    */
    bool
    trustedPublisher(PublicKey const& identity) const;

    /** Returns local validator public key

        @par Thread Safety

        May be called concurrently
    */
    PublicKey
    localPublicKey() const;

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
    for_each_listed(std::function<void(PublicKey const&, bool)> func) const;

    /** Invokes the callback once for every available publisher list's raw
        data members

        @note Undefined behavior results when calling ValidatorList members
        from within the callback

        The arguments passed into the lambda are:

        @li The raw manifest string

        @li The raw "blob" string containing the values for the validator list

        @li The signature string used to sign the blob

        @li The version number

        @li The `PublicKey` of the blob signer (matches the value from
            [validator_list_keys])

        @li The sequence number of the "blob"

        @li The precomputed hash of the original / raw elements

        @par Thread Safety

        May be called concurrently
    */
    void
    for_each_available(std::function<void(
                           std::string const& manifest,
                           std::string const& blob,
                           std::string const& signature,
                           std::uint32_t version,
                           PublicKey const& pubKey,
                           std::size_t sequence,
                           uint256 const& hash)> func) const;

    /** Returns the current valid list for the given publisher key,
        if available, as a Json object.
    */
    boost::optional<Json::Value>
    getAvailable(boost::beast::string_view const& pubKey);

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

    using QuorumKeys = std::pair<std::size_t const, hash_set<PublicKey>>;
    /** Get the quorum and all of the trusted keys.
     *
     * @return quorum and keys.
     */
    QuorumKeys
    getQuorumKeys() const
    {
        shared_lock read_lock{mutex_};
        return {quorum_, trustedSigningKeys_};
    }

    /**
     * get the trusted master public keys
     * @return the public keys
     */
    hash_set<PublicKey>
    getTrustedMasterKeys() const;

    /**
     * get the master public keys of Negative UNL validators
     * @return the master public keys
     */
    hash_set<PublicKey>
    getNegativeUNL() const;

    /**
     * set the Negative UNL with validators' master public keys
     * @param negUnl the public keys
     */
    void
    setNegativeUNL(hash_set<PublicKey> const& negUnl);

    /**
     * Remove validations that are from validators on the negative UNL.
     *
     * @param validations  the validations to filter
     * @return a filtered copy of the validations
     */
    std::vector<std::shared_ptr<STValidation>>
    negativeUNLFilter(
        std::vector<std::shared_ptr<STValidation>>&& validations) const;

private:
    /** Return the number of configured validator list sites. */
    std::size_t
    count(shared_lock const&) const;

    /** Returns `true` if public key is trusted

    @param identity Validation public key

    @par Thread Safety

    May be called concurrently
    */
    bool
    trusted(shared_lock const&, PublicKey const& identity) const;

    /** Returns master public key if public key is trusted

    @param identity Validation public key

    @return `boost::none` if key is not trusted

    @par Thread Safety

    May be called concurrently
    */
    boost::optional<PublicKey>
    getTrustedKey(shared_lock const&, PublicKey const& identity) const;

    /** Return the time when the validator list will expire

    @note This may be a time in the past if a published list has not
    been updated since its expiration. It will be boost::none if any
    configured published list has not been fetched.

    @par Thread Safety
    May be called concurrently
    */
    boost::optional<TimeKeeper::time_point>
    expires(shared_lock const&) const;

    /** Get the filename used for caching UNLs
     */
    boost::filesystem::path
    GetCacheFileName(unique_lock const&, PublicKey const& pubKey);

    /** Write a JSON UNL to a cache file
     */
    void
    CacheValidatorFile(
        unique_lock const& lock,
        PublicKey const& pubKey,
        PublisherList const& publisher);

    /** Check response for trusted valid published list

        @return `ListDisposition::accepted` if list can be applied

        @par Thread Safety

        Calling public member function is expected to lock mutex
    */
    ListDisposition
    verify(
        unique_lock const&,
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
    removePublisherList(unique_lock const&, PublicKey const& publisherKey);

    /** Return quorum for trusted validator set

        @param unlSize Number of trusted validator keys

        @param effectiveUnlSize Number of trusted validator keys that are not in
        the NegativeUNL

        @param seenSize Number of trusted validators that have signed
        recently received validations
    */
    std::size_t
    calculateQuorum(
        std::size_t unlSize,
        std::size_t effectiveUnlSize,
        std::size_t seenSize);
};
}  // namespace ripple

#endif
