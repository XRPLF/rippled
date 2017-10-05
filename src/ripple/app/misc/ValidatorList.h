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
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <mutex>
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
        bool available;
        std::vector<PublicKey> list;
        std::size_t sequence;
        TimeKeeper::time_point expiration;
    };

    ManifestCache& validatorManifests_;
    ManifestCache& publisherManifests_;
    TimeKeeper& timeKeeper_;
    beast::Journal j_;
    boost::shared_mutex mutable mutex_;

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

    // The minimum number of listed validators required to allow removing
    // non-communicative validators from the trusted set. In other words, if the
    // number of listed validators is less, then use all of them in the
    // trusted set.
    std::size_t const MINIMUM_RESIZEABLE_UNL {25};
    // The maximum size of a trusted set for which greater than Byzantine fault
    // tolerance isn't needed.
    std::size_t const BYZANTINE_THRESHOLD {32};



public:
    ValidatorList (
        ManifestCache& validatorManifests,
        ManifestCache& publisherManifests,
        TimeKeeper& timeKeeper,
        beast::Journal j,
        boost::optional<std::size_t> minimumQuorum = boost::none);
    ~ValidatorList ();

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
        std::uint32_t version);

    /** Update trusted keys

        Reset the trusted keys based on latest manifests, received validations,
        and lists.

        @param seenValidators Set of public keys used to sign recently
        received validations

        @par Thread Safety

        May be called concurrently
    */
    template<class KeySet>
    void
    onConsensusStart (
        KeySet const& seenValidators);

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
    };

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

    /** Return safe minimum quorum for listed validator set

        @param nListedKeys Number of list validator keys

        @param unListedLocal Whether the local node is an unlisted validator
    */
    static std::size_t
    calculateMinimumQuorum (
        std::size_t nListedKeys, bool unlistedLocal=false);
};

//------------------------------------------------------------------------------

template<class KeySet>
void
ValidatorList::onConsensusStart (
    KeySet const& seenValidators)
{
    boost::unique_lock<boost::shared_mutex> lock{mutex_};

    // Check that lists from all configured publishers are available
    bool allListsAvailable = true;

    for (auto const& list : publisherLists_)
    {
        // Remove any expired published lists
        if (TimeKeeper::time_point{} < list.second.expiration &&
            list.second.expiration <= timeKeeper_.now())
            removePublisherList(list.first);

        if (! list.second.available)
            allListsAvailable = false;
    }

    std::multimap<std::size_t, PublicKey> rankedKeys;
    bool localKeyListed = false;

    // "Iterate" the listed keys in random order so that the rank of multiple
    // keys with the same number of listings is not deterministic
    std::vector<std::size_t> indexes (keyListings_.size());
    std::iota (indexes.begin(), indexes.end(), 0);
    std::shuffle (indexes.begin(), indexes.end(), crypto_prng());

    for (auto const& index : indexes)
    {
        auto const& val = std::next (keyListings_.begin(), index);

        if (validatorManifests_.revoked (val->first))
            continue;

        if (val->first == localPubKey_)
        {
            localKeyListed = val->second > 1;
            rankedKeys.insert (
                std::pair<std::size_t,PublicKey>(
                    std::numeric_limits<std::size_t>::max(), localPubKey_));
        }
        // If the total number of validators is too small, or
        // no validations are being received, use all validators.
        // Otherwise, do not use validators whose validations aren't
        // being received.
        else if (keyListings_.size() < MINIMUM_RESIZEABLE_UNL ||
                 seenValidators.empty() ||
                 seenValidators.find (val->first) != seenValidators.end ())
        {
            rankedKeys.insert (
                std::pair<std::size_t,PublicKey>(val->second, val->first));
        }
    }

    // This minimum quorum guarantees safe overlap with the trusted sets of
    // other nodes using the same set of published lists.
    std::size_t quorum = calculateMinimumQuorum (keyListings_.size(),
        localPubKey_.size() && !localKeyListed);

    JLOG (j_.debug()) <<
        rankedKeys.size() << "  of " << keyListings_.size() <<
        " listed validators eligible for inclusion in the trusted set";

    auto size = rankedKeys.size();

    // Require 80% quorum if there are lots of validators.
    if (rankedKeys.size() > BYZANTINE_THRESHOLD)
    {
        // Use all eligible keys if there is only one trusted list
        if (publisherLists_.size() == 1 ||
                keyListings_.size() < MINIMUM_RESIZEABLE_UNL)
        {
            // Try to raise the quorum to at least 80% of the trusted set
            quorum = std::max(quorum, size - size / 5);
        }
        else
        {
            // Reduce the trusted set size so that the quorum represents
            // at least 80%
            size = quorum * 1.25;
        }
    }

    if (minimumQuorum_ && seenValidators.size() < quorum)
    {
        quorum = *minimumQuorum_;
        JLOG (j_.warn())
            << "Using unsafe quorum of "
            << quorum_
            << " as specified in the command line";
    }

    // Do not use achievable quorum until lists from all configured
    // publishers are available
    else if (! allListsAvailable)
        quorum = std::numeric_limits<std::size_t>::max();

    trustedKeys_.clear();
    quorum_ = quorum;

    for (auto const& val : boost::adaptors::reverse (rankedKeys))
    {
        if (size <= trustedKeys_.size())
            break;

        trustedKeys_.insert (val.second);
    }

    JLOG (j_.debug()) <<
        "Using quorum of " << quorum_ << " for new set of " <<
        trustedKeys_.size() << " trusted validators";

    if (trustedKeys_.size() < quorum_)
    {
        JLOG (j_.warn()) <<
            "New quorum of " << quorum_ <<
            " exceeds the number of trusted validators (" <<
            trustedKeys_.size() << ")";
    }
}

} // ripple

#endif
