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
#include <ripple/overlay/Message.h>
#include <ripple/protocol/PublicKey.h>
#include <boost/iterator/counting_iterator.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <mutex>
#include <numeric>
#include <shared_mutex>

namespace protocol {
class TMValidatorList;
class TMValidatorListCollection;
}  // namespace protocol

namespace ripple {

class Overlay;
class HashRouter;
class Message;
class NetworkOPs;
class Peer;
class STValidation;

/* Entries in this enum are ordered by "desirability".
   The "better" dispositions have lower values than the
   "worse" dispositions */
enum class ListDisposition {
    /// List is valid
    accepted = 0,

    /// List is expired, but has the largest non-pending sequence seen so far
    expired,

    /// List will be valid in the future
    pending,

    /// Same sequence as current list
    same_sequence,

    /// Future sequence already seen
    known_sequence,

    /// Trusted publisher key, but seq is too old
    stale,

    /// List signed by untrusted publisher key
    untrusted,

    /// List version is not supported
    unsupported_version,

    /// Invalid format or signature
    invalid
};

/* Entries in this enum are ordered by "desirability".
   The "better" dispositions have lower values than the
   "worse" dispositions */
enum class PublisherStatus {
    // Publisher has provided a valid file
    available = 0,

    // Current list is expired without replacement
    expired,

    // No file seen yet
    unavailable,

    // Publisher has revoked their manifest key
    revoked,

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

/** Used to represent the information stored in the blobs_v2 Json array */
struct ValidatorBlobInfo
{
    // base-64 encoded JSON containing the validator list.
    std::string blob;
    // hex-encoded signature of the blob using the publisher's signing key
    std::string signature;
    // base-64 or hex-encoded manifest containing the publisher's master and
    // signing public keys
    std::optional<std::string> manifest;
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
        "validFrom", @c "validUntil", and @c "validators" field. @c "validFrom"
        contains the Ripple timestamp (seconds since January 1st, 2000 (00:00
        UTC)) for when the list becomes valid. @c "validUntil" contains the
        Ripple timestamp for when the list expires. @c "validators" contains
        an array of objects with a @c "validation_public_key" and optional
        @c "manifest" field. @c "validation_public_key" should be the
        hex-encoded master public key. @c "manifest" should be the
        base64-encoded validator manifest.

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

        std::vector<PublicKey> list;
        std::vector<std::string> manifests;
        std::size_t sequence;
        TimeKeeper::time_point validFrom;
        TimeKeeper::time_point validUntil;
        std::string siteUri;
        // base-64 encoded JSON containing the validator list.
        std::string rawBlob;
        // hex-encoded signature of the blob using the publisher's signing key
        std::string rawSignature;
        // base-64 or hex-encoded manifest containing the publisher's master and
        // signing public keys
        std::optional<std::string> rawManifest;
        uint256 hash;
    };

    struct PublisherListCollection
    {
        PublisherStatus status;
        /*
        The `current` VL is the one which
         1. Has the largest sequence number that
         2. Has ever been effective (the effective date is absent or in the
            past).
        If this VL has expired, all VLs with previous sequence numbers
        will also be considered expired, and thus there will be no valid VL
        until one with a larger sequence number becomes effective. This is to
        prevent allowing old VLs to reactivate.
        */
        PublisherList current;
        /*
        The `remaining` list holds any relevant VLs which have a larger sequence
        number than current. By definition they will all have an effective date
        in the future. Relevancy will be determined by sorting the VLs by
        sequence number, then iterating over the list and removing any VLs for
        which the following VL (ignoring gaps) has the same or earlier effective
        date.
        */
        std::map<std::size_t, PublisherList> remaining;
        std::optional<std::size_t> maxSequence;
        // The hash of the full set if sent in a single message
        uint256 fullHash;
        std::string rawManifest;
        std::uint32_t rawVersion = 0;
    };

    ManifestCache& validatorManifests_;
    ManifestCache& publisherManifests_;
    TimeKeeper& timeKeeper_;
    boost::filesystem::path const dataPath_;
    beast::Journal const j_;
    boost::shared_mutex mutable mutex_;
    using lock_guard = std::lock_guard<decltype(mutex_)>;
    using shared_lock = std::shared_lock<decltype(mutex_)>;

    std::atomic<std::size_t> quorum_;
    std::optional<std::size_t> minimumQuorum_;

    // Published lists stored by publisher master public key
    hash_map<PublicKey, PublisherListCollection> publisherLists_;

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

    // Currently supported versions of publisher list format
    static constexpr std::uint32_t supportedListVersions[]{1, 2};
    // In the initial release, to prevent potential abuse and attacks, any VL
    // collection with more than 5 entries will be considered malformed.
    static constexpr std::size_t maxSupportedBlobs = 5;
    // Prefix of the file name used to store cache files.
    static const std::string filePrefix_;

public:
    ValidatorList(
        ManifestCache& validatorManifests,
        ManifestCache& publisherManifests,
        TimeKeeper& timeKeeper,
        std::string const& databasePath,
        beast::Journal j,
        std::optional<std::size_t> minimumQuorum = std::nullopt);
    ~ValidatorList() = default;

    /** Describes the result of processing a Validator List (UNL),
    including some of the information from the list which can
    be used by the caller to know which list publisher is
    involved.
    */
    struct PublisherListStats
    {
        explicit PublisherListStats() = default;
        explicit PublisherListStats(ListDisposition d);
        PublisherListStats(
            ListDisposition d,
            PublicKey key,
            PublisherStatus stat,
            std::size_t seq);

        ListDisposition
        bestDisposition() const;
        ListDisposition
        worstDisposition() const;
        void
        mergeDispositions(PublisherListStats const& src);

        // Tracks the dispositions of each processed list and how many times it
        // occurred
        std::map<ListDisposition, std::size_t> dispositions;
        std::optional<PublicKey> publisherKey;
        PublisherStatus status = PublisherStatus::unavailable;
        std::size_t sequence = 0;
    };

    struct MessageWithHash
    {
        explicit MessageWithHash() = default;
        explicit MessageWithHash(
            std::shared_ptr<Message> const& message_,
            uint256 hash_,
            std::size_t num_);
        std::shared_ptr<Message> message;
        uint256 hash;
        std::size_t numVLs = 0;
    };

    /** Load configured trusted keys.

        @param localSigningKey This node's validation public key

        @param configKeys List of trusted keys from config. Each entry
        consists of a base58 encoded validation public key, optionally followed
        by a comment.

        @param publisherKeys List of trusted publisher public keys. Each
        entry contains a base58 encoded account public key.

        @par Thread Safety

        May be called concurrently

        @return `false` if an entry is invalid or unparsable
    */
    bool
    load(
        PublicKey const& localSigningKey,
        std::vector<std::string> const& configKeys,
        std::vector<std::string> const& publisherKeys);

    /** Pull the blob/signature/manifest information out of the appropriate Json
        body fields depending on the version.

        @return An empty vector indicates malformed Json.
     */
    static std::vector<ValidatorBlobInfo>
    parseBlobs(std::uint32_t version, Json::Value const& body);

    static std::vector<ValidatorBlobInfo>
    parseBlobs(protocol::TMValidatorList const& body);

    static std::vector<ValidatorBlobInfo>
    parseBlobs(protocol::TMValidatorListCollection const& body);

    static void
    sendValidatorList(
        Peer& peer,
        std::uint64_t peerSequence,
        PublicKey const& publisherKey,
        std::size_t maxSequence,
        std::uint32_t rawVersion,
        std::string const& rawManifest,
        std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
        HashRouter& hashRouter,
        beast::Journal j);

    [[nodiscard]] static std::pair<std::size_t, std::size_t>
    buildValidatorListMessages(
        std::size_t messageVersion,
        std::uint64_t peerSequence,
        std::size_t maxSequence,
        std::uint32_t rawVersion,
        std::string const& rawManifest,
        std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
        std::vector<MessageWithHash>& messages,
        std::size_t maxSize = maximiumMessageSize);

    /** Apply multiple published lists of public keys, then broadcast it to all
        peers that have not seen it or sent it.

        @param manifest base64-encoded publisher key manifest

        @param version Version of published list format

        @param blobs Vector of BlobInfos representing one or more encoded
            validator lists and signatures (and optional manifests)

        @param siteUri Uri of the site from which the list was validated

        @param hash Hash of the data parameters

        @param overlay Overlay object which will handle sending the message

        @param hashRouter HashRouter object which will determine which
            peers not to send to

        @param networkOPs NetworkOPs object which will be informed if there
            is a valid VL

        @return `ListDisposition::accepted`, plus some of the publisher
            information, if list was successfully applied

        @par Thread Safety

        May be called concurrently
    */
    PublisherListStats
    applyListsAndBroadcast(
        std::string const& manifest,
        std::uint32_t version,
        std::vector<ValidatorBlobInfo> const& blobs,
        std::string siteUri,
        uint256 const& hash,
        Overlay& overlay,
        HashRouter& hashRouter,
        NetworkOPs& networkOPs);

    /** Apply multiple published lists of public keys.

        @param manifest base64-encoded publisher key manifest

        @param version Version of published list format

        @param blobs Vector of BlobInfos representing one or more encoded
        validator lists and signatures (and optional manifests)

        @param siteUri Uri of the site from which the list was validated

        @param hash Optional hash of the data parameters

        @return `ListDisposition::accepted`, plus some of the publisher
        information, if list was successfully applied

        @par Thread Safety

        May be called concurrently
    */
    PublisherListStats
    applyLists(
        std::string const& manifest,
        std::uint32_t version,
        std::vector<ValidatorBlobInfo> const& blobs,
        std::string siteUri,
        std::optional<uint256> const& hash = {});

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
    updateTrusted(
        hash_set<NodeID> const& seenValidators,
        NetClock::time_point closeTime,
        NetworkOPs& ops,
        Overlay& overlay,
        HashRouter& hashRouter);

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

        @return `std::nullopt` if key is not trusted

        @par Thread Safety

        May be called concurrently
    */
    std::optional<PublicKey>
    getTrustedKey(PublicKey const& identity) const;

    /** Returns listed master public if public key is included on any lists

        @param identity Validation public key

        @return `std::nullopt` if key is not listed

        @par Thread Safety

        May be called concurrently
    */
    std::optional<PublicKey>
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
    for_each_available(
        std::function<void(
            std::string const& manifest,
            std::uint32_t version,
            std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
            PublicKey const& pubKey,
            std::size_t maxSequence,
            uint256 const& hash)> func) const;

    /** Returns the current valid list for the given publisher key,
        if available, as a Json object.
    */
    std::optional<Json::Value>
    getAvailable(
        boost::beast::string_view const& pubKey,
        std::optional<std::uint32_t> forceVersion = {});

    /** Return the number of configured validator list sites. */
    std::size_t
    count() const;

    /** Return the time when the validator list will expire

        @note This may be a time in the past if a published list has not
        been updated since its validUntil. It will be std::nullopt if any
        configured published list has not been fetched.

        @par Thread Safety
        May be called concurrently
    */
    std::optional<TimeKeeper::time_point>
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

    @return `std::nullopt` if key is not trusted

    @par Thread Safety

    May be called concurrently
    */
    std::optional<PublicKey>
    getTrustedKey(shared_lock const&, PublicKey const& identity) const;

    /** Return the time when the validator list will expire

    @note This may be a time in the past if a published list has not
    been updated since its expiration. It will be std::nullopt if any
    configured published list has not been fetched.

    @par Thread Safety
    May be called concurrently
    */
    std::optional<TimeKeeper::time_point>
    expires(shared_lock const&) const;

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
        std::string const& globalManifest,
        std::optional<std::string> const& localManifest,
        std::string const& blob,
        std::string const& signature,
        std::uint32_t version,
        std::string siteUri,
        std::optional<uint256> const& hash,
        lock_guard const&);

    void
    updatePublisherList(
        PublicKey const& pubKey,
        PublisherList const& current,
        std::vector<PublicKey> const& oldList,
        lock_guard const&);

    static void
    buildBlobInfos(
        std::map<std::size_t, ValidatorBlobInfo>& blobInfos,
        PublisherListCollection const& lists);

    static std::map<std::size_t, ValidatorBlobInfo>
    buildBlobInfos(PublisherListCollection const& lists);

    static void
    broadcastBlobs(
        PublicKey const& publisherKey,
        PublisherListCollection const& lists,
        std::size_t maxSequence,
        uint256 const& hash,
        Overlay& overlay,
        HashRouter& hashRouter,
        beast::Journal j);

    static void
    sendValidatorList(
        Peer& peer,
        std::uint64_t peerSequence,
        PublicKey const& publisherKey,
        std::size_t maxSequence,
        std::uint32_t rawVersion,
        std::string const& rawManifest,
        std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
        std::vector<MessageWithHash>& messages,
        HashRouter& hashRouter,
        beast::Journal j);

    /** Get the filename used for caching UNLs
     */
    boost::filesystem::path
    getCacheFileName(lock_guard const&, PublicKey const& pubKey) const;

    /** Build a Json representation of the collection, suitable for
        writing to a cache file, or serving to a /vl/ query
    */
    static Json::Value
    buildFileData(
        std::string const& pubKey,
        PublisherListCollection const& pubCollection,
        beast::Journal j);

    /** Build a Json representation of the collection, suitable for
    writing to a cache file, or serving to a /vl/ query
    */
    static Json::Value
    buildFileData(
        std::string const& pubKey,
        PublisherListCollection const& pubCollection,
        std::optional<std::uint32_t> forceVersion,
        beast::Journal j);

    template <class Hasher>
    friend void
    hash_append(Hasher& h, PublisherListCollection pl)
    {
        using beast::hash_append;
        hash_append(h, pl.rawManifest, buildBlobInfos(pl), pl.rawVersion);
    }

    /** Write a JSON UNL to a cache file
     */
    void
    cacheValidatorFile(lock_guard const& lock, PublicKey const& pubKey) const;

    /** Check response for trusted valid published list

        @return `ListDisposition::accepted` if list can be applied

        @par Thread Safety

        Calling public member function is expected to lock mutex
    */
    ListDisposition
    verify(
        lock_guard const&,
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
    removePublisherList(
        lock_guard const&,
        PublicKey const& publisherKey,
        PublisherStatus reason);

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

// hashing helpers
template <class Hasher>
void
hash_append(Hasher& h, ValidatorBlobInfo const& blobInfo)
{
    using beast::hash_append;
    hash_append(h, blobInfo.blob, blobInfo.signature);
    if (blobInfo.manifest)
    {
        hash_append(h, *blobInfo.manifest);
    }
}

template <class Hasher>
void
hash_append(Hasher& h, std::vector<ValidatorBlobInfo> const& blobs)
{
    for (auto const& item : blobs)
        hash_append(h, item);
}

template <class Hasher>
void
hash_append(Hasher& h, std::map<std::size_t, ValidatorBlobInfo> const& blobs)
{
    for (auto const& [_, item] : blobs)
    {
        (void)_;
        hash_append(h, item);
    }
}

}  // namespace ripple

namespace protocol {

template <class Hasher>
void
hash_append(Hasher& h, TMValidatorList const& msg)
{
    using beast::hash_append;
    hash_append(h, msg.manifest(), msg.blob(), msg.signature(), msg.version());
}

template <class Hasher>
void
hash_append(Hasher& h, TMValidatorListCollection const& msg)
{
    using beast::hash_append;
    hash_append(
        h,
        msg.manifest(),
        ripple::ValidatorList::parseBlobs(msg),
        msg.version());
}

}  // namespace protocol

#endif
