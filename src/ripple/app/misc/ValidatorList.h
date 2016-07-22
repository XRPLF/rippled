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

#include <ripple/app/misc/detail/Work.h>
#include <ripple/app/misc/Manifest.h>
#include <ripple/app/misc/Validations.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/protocol/PublicKey.h>
#include <boost/asio.hpp>
#include <mutex>

namespace ripple {

/*
    Validator Lists
    ---------------

    Rippled accepts ledger proposals and validations from trusted validator
    nodes. A ledger is considered fully-validated once the number of received
    trusted validations for a ledger meets or exceeds a quorum value.

    This class manages the set of validators the local rippled node trusts.
    The list of trusted validators is populated using the validators listed in
    the configuration file as well as remote sites that serve validators
    recommended by trusted publishers. The site URIs and trusted publisher
    public keys are specified in the config.

    The validator lists from the remote sites are fetched at a regular interval.
    Fetched lists are expected to be in JSON format and contain the following
    fields:
    - "manifest": Base64-encoded serialization of a manifest containing the
        publisher's master and signing public keys. This has the same format as
        the [validation_manifest] config section.
    - "blob": Base64-encoded JSON string containing a "sequence" and
        "validators" field. "validators" contains an array of objects with
        "validation_public_key" and "manifest" fields.
        "validation_public_key" must be an Ed25519 master public key.
    - "signature": Hex-encoded signature of the blob using the publisher's
        signing key.
    - "version": 1
    - "refreshInterval": (optional) recommended fetch interval in minutes

    Individual validator lists are stored separately by publisher. The number of
    lists on which a validator appears is also tracked.

    The list of trusted validators is reset at the start of each consensus round
    to take into account the latest fetched lists as well as the set of
    validators from whom validations are being received. Listed validators are
    shuffled and then sorted by the number of lists they appear on. (The
    shuffling makes the order/rank of validators with the same number of
    listings non-deterministic. If there is only one list, all listed validators
    are trusted. Otherwise, the trusted list size set to 80% of the median list
    length. Finally, a quorum value is determined for the new trusted validator
    list.
*/

//------------------------------------------------------------------------------

class ValidatorList
{
    friend class Work;

private:
    using error_code = boost::system::error_code;
    using clock_type = std::chrono::system_clock;

    struct Site
    {
        std::string uri;
        parsedURL pUrl;
        std::chrono::minutes refreshInterval;
        clock_type::time_point nextRefresh;
        bool fetched;
    };

    struct PublisherList
    {
        std::vector<PublicKey> list;
        int sequence;
    };

    ManifestCache& manifests_;
    beast::Journal j_;
    std::mutex mutable mutex_;
    std::condition_variable cv_;
    std::weak_ptr<detail::Work> work_;
    boost::asio::io_service& io_service_;
    boost::asio::basic_waitable_timer<clock_type> timer_;

    bool pending_;
    bool stopping_;

    std::size_t fetchedSites_;
    std::size_t fetchedLists_;
    int quorum_;

    /** The configured list of URIs for fetching validator lists */
    std::vector<Site> sites_;

    /** Published validator lists stored by publisher master public key */
    hash_map<PublicKey, PublisherList> publisher_lists_;

    /** Listed validator master public keys with
        the number of lists they appear on */
    hash_map<PublicKey, std::size_t> validatorListings_;

    /** The current list of trusted validator signing keys */
    hash_set<PublicKey> validators_;

public:
    ValidatorList (
        ManifestCache& manifests,
        boost::asio::io_service& io_service,
        beast::Journal j);
    ~ValidatorList ();

    /// Determines whether a node is trusted
    bool
    trusted (
        PublicKey const& identity) const;

    /// Determines whether a node is included on any recommended validator lists
    bool
    listed (
        PublicKey const& identity) const;

    /// Determine whether a validator list publisher key is trusted
    bool
    trustedPublisher (
        PublicKey const& identity) const;

    /** Invokes the callback once for every listed validator.
        @note You are not allowed to insert or remove any
              validators from within the callback.

        The arguments passed into the lambda are:
            - The public key of the validator;
            - A boolean indicating whether this is a
              trusted key;
    */
    void
    for_each_listed (
        std::function<void(PublicKey const&, bool)> func) const;

    /** Load the list of trusted validators and validator list sites and keys.

        The list of validators contains entries consisting of a base58
        encoded validator public key, optionally followed by
        a comment.

        The list of validator list sites contains URIs to fetch recommended
        validators.

        The list of validator list keys contains base58 encoded account keys
        used by trusted publishers to sign lists of recommended validators.

        @return false if an entry could not be parsed or
                contained an invalid validator public key,
                true otherwise.
    */
    bool
    load (
        PublicKey const& localSigningKey,
        std::vector<std::string> const& configValidators,
        std::vector<std::string> const& validatorListSites,
        std::vector<std::string> const& validatorListKeys,
        std::vector<std::string> const& configManifest);

    /** Update trusted validator list

        Reset the list based on latest lists from validator sites, known active
        validators, and stored validator manifests.
    */
    void
    update (
        ValidationSet const& activeValidators);

    bool
    removeList (PublicKey const& publisherKey);

    int
    quorum () const
    {
        return quorum_;
    };

    int
    getFetchedSitesCount () const
    {
        return fetchedSites_;
    }

    /// Determines the quorum for the trusted validator list size.
    static int
    calcQuorum (
        std::uint32_t const& nTrusted,
        std::uint32_t const& nListed);

    void
    stop ();

private:
    void
    setTimer ();

    void
    onTimer (
        std::size_t siteIdx,
        error_code const& ec);

    bool
    verifyResponse (
        detail::response_type const& res,
        std::size_t const& siteIdx,
        Json::Value& body,
        Json::Value& list,
        PublicKey& pubKey);

    void
    onWork (
        boost::system::error_code const& ec,
        detail::response_type&& res,
        std::size_t siteIdx);

    int
    getTargetSize () const;
};

} // ripple

#endif
