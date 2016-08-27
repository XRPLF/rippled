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

#include <ripple/app/misc/detail/WorkPlain.h>
#include <ripple/app/misc/detail/WorkSSL.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/basics/Slice.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/json/json_reader.h>
#include <beast/core/detail/base64.hpp>
#include <beast/core/placeholders.hpp>
#include <boost/regex.hpp>

namespace ripple {

// validator site default query frequency - 5 minutes
auto constexpr DEFAULT_REFRESH_INTERVAL = std::chrono::minutes{5};
auto constexpr MIN_NETWORK_VALIDATOR_LIST_OVERLAP = 0.4;

ValidatorList::ValidatorList (
    ManifestCache& manifests,
    boost::asio::io_service& io_service,
    beast::Journal j)
    : manifests_ (manifests)
    , j_ (j)
    , io_service_ (io_service)
    , timer_ (io_service)
    , pending_ (false)
    , stopping_ (false)
    , fetchedSites_ (0)
    , fetchedLists_ (0)
    , quorum_ (0)
{
}

ValidatorList::~ValidatorList()
{
    if (! stopping_)
        stop();
    std::unique_lock<std::mutex> lock{mutex_};
    cv_.wait(lock, [&]{ return ! pending_; });
}

void
ValidatorList::stop()
{
    {
        std::lock_guard<std::mutex> lock{mutex_};
        stopping_ = true;
    }
    if(auto sp = work_.lock())
        sp->cancel();
    error_code ec;
    timer_.cancel(ec);
}

bool
ValidatorList::verifyResponse (
    detail::response_type const& res,
    std::size_t const& siteIdx,
    Json::Value& body,
    Json::Value& list,
    PublicKey& pubKey)
{
    if (res.status != 200)
    {
        JLOG (j_.warn()) <<
            "Request for validator list at " <<
            sites_[siteIdx].uri << " returned " << res.status;
        return false;
    }

    Json::Reader r;
    if (! r.parse(res.body.data(), body))
    {
        JLOG (j_.warn()) <<
            "Unable to parse JSON response from  " <<
            sites_[siteIdx].uri;
        return false;
    }

    if (body.isObject () &&
        body.isMember("blob") && body["blob"].isString () &&
        body.isMember("manifest") && body["manifest"].isString () &&
        body.isMember("signature") && body["signature"].isString() &&
        body.isMember("version") && body["version"].isInt())
    {
        if (body["version"].asInt() != 1)
        {
            JLOG (j_.warn()) <<
                "Invalid version from " << sites_[siteIdx].uri;
            return false;
        }

        auto manifest = make_Manifest (beast::detail::base64_decode(
            body["manifest"].asString ()));

        ManifestDisposition result;
        if (manifest)
            result = manifests_.applyManifest (
                Manifest (
                    manifest->serialized, manifest->masterKey,
                    manifest->signingKey, manifest->sequence),
                *this);

        if (! manifest || result == ManifestDisposition::invalid)
        {
            JLOG (j_.warn()) <<
                "Invalid manifest from " << sites_[siteIdx].uri;
            return false;
        }
        else if (result == ManifestDisposition::untrusted)
        {
            JLOG (j_.warn()) <<
                "Untrusted validator list signing public key (" <<
                toBase58(TokenType::TOKEN_NODE_PUBLIC, manifest->masterKey) <<
                ") from " << sites_[siteIdx].uri;
            return false;
        }
        else if (manifest->revoked())
        {
            if (result == ManifestDisposition::accepted)
            {
                JLOG (j_.debug()) <<
                    "Revoked validator list signing public key (" <<
                    toBase58(
                        TokenType::TOKEN_NODE_PUBLIC, manifest->masterKey) <<
                    ") from " << sites_[siteIdx].uri;
            }
            return false;
        }

        pubKey = manifest->masterKey;
        auto iter = publisher_lists_.find (pubKey);
        if (iter == publisher_lists_.end ())
        {
            JLOG (j_.warn()) <<
                "Untrusted validator list signing public key (" <<
                toBase58(TokenType::TOKEN_NODE_PUBLIC, pubKey) <<
                ") from " << sites_[siteIdx].uri;
            return false;
        }

        auto const blob =
            beast::detail::base64_decode (body["blob"].asString());
        r.parse (blob, list);

        if (list.isMember("sequence") && list["sequence"].isInt() &&
            list.isMember("validators") && list["validators"].isArray())
        {
            auto const sequence = list["sequence"].asInt ();
            if (sequence <= iter->second.sequence)
            {
                JLOG (j_.debug()) <<
                    sites_[siteIdx].uri <<
                    " returned stale validator list for " <<
                    toBase58(TokenType::TOKEN_NODE_PUBLIC, pubKey);
                return false;
            }

            auto const sig = strUnHex(body["signature"].asString ());
            if (! sig.second ||
                ! verify (
                    manifest->signingKey,
                    makeSlice(blob),
                    makeSlice(sig.first)))
            {
                JLOG (j_.debug()) <<
                    sites_[siteIdx].uri <<
                    " returned invalid validator list signature";
                return false;
            }
        }
        else
        {
            JLOG (j_.warn()) <<
                "Unable to verify validator list signature from  " <<
                sites_[siteIdx].uri;
            return false;
        }
    }
    else
    {
        JLOG (j_.warn()) <<
            "Unable to verify validator list signature from  " <<
            sites_[siteIdx].uri;
        return false;
    }

    return true;
}

void
ValidatorList::onWork(
    boost::system::error_code const& ec,
    detail::response_type&& res,
    std::size_t siteIdx)
{
    Json::Value body;
    Json::Value list;
    PublicKey pubKey;
    std::vector<std::string> manifests;

    if (! ec && verifyResponse (res, siteIdx, body, list, pubKey))
    {
        std::lock_guard <std::mutex> lock{mutex_};

        // Update publisher's validator list
        Json::Value const& newList = list["validators"];
        std::vector<PublicKey>& publisherList = publisher_lists_[pubKey].list;

        if (publisherList.empty() && newList.size())
            ++fetchedLists_;
        std::vector<PublicKey> oldList = publisherList;
        publisherList.clear ();
        publisherList.reserve (newList.size ());
        for (auto const& val : newList)
        {
            if (val.isObject () &&
                val.isMember ("validation_public_key") &&
                val["validation_public_key"].isString ())
            {
                auto const id = parseBase58<PublicKey>(
                    TokenType::TOKEN_NODE_PUBLIC,
                    val["validation_public_key"].asString ());

                if (! id)
                {
                    JLOG (j_.error()) <<
                        "Invalid node identity: " <<
                        val["validation_public_key"].asString ();
                }
                else if (publicKeyType(*id) == KeyType::ed25519)
                {
                    publisherList.push_back (*id);

                    if (val.isMember ("validation_manifest") &&
                        val["validation_manifest"].isString ())
                    {
                        manifests.push_back (
                            val["validation_manifest"].asString ());
                    }
                }
                else
                {
                    JLOG (j_.warn()) <<
                        "Invalid secp256k1 validator key listed at " <<
                        sites_[siteIdx].uri;
                }
            }
        }

        // Update validatorListings_ for added and removed validators
        std::sort (
            publisherList.begin (),
            publisherList.end ());

        auto iNew = publisherList.begin ();
        auto iOld = oldList.begin ();
        while (iNew != publisherList.end () ||
            iOld != oldList.end ())
        {
            if (iOld == oldList.end () ||
                (iNew != publisherList.end () &&
                *iNew < *iOld))
            {
                // Increment list count for added validators
                ++validatorListings_[*iNew];
                ++iNew;
            }
            else if (iNew == publisherList.end () ||
                (iOld != oldList.end () && *iOld < *iNew))
            {
                // Decrement list count for removed validators
                if (validatorListings_[*iOld] == 1)
                    validatorListings_.erase (*iOld);
                else
                    --validatorListings_[*iOld];
                ++iOld;
            }
            else
            {
                ++iNew;
                ++iOld;
            }
        }

        if (publisherList.empty())
        {
            JLOG (j_.warn()) <<
                "No validators listed at " << sites_[siteIdx].uri;
        }

        if (body.isMember ("refresh_interval") && 
            body["refresh_interval"].isNumeric ())
        {
            sites_[siteIdx].refreshInterval =
                std::chrono::minutes{body["refresh_interval"].asInt ()};
        }
    }

    for (auto const& manifest : manifests)
    {
        if (auto mo = make_Manifest (
            beast::detail::base64_decode(manifest)))
        {
            manifests_.applyManifest (
                std::move (*mo),
                *this);
        }
    }

    std::lock_guard <std::mutex> lock{mutex_};
    if (! ec && ! sites_[siteIdx].fetched)
    {
        sites_[siteIdx].fetched = true;
        ++fetchedSites_;
    }

    pending_ = false;
    cv_.notify_one();
    if (! stopping_)
        setTimer ();
}

bool
ValidatorList::listed (
    PublicKey const& identity) const
{
    auto const masterKey = manifests_.getMasterKey (identity);
    auto const pubKey = masterKey ? *masterKey : identity;
    return validatorListings_.find (pubKey) != validatorListings_.end ();
}

bool
ValidatorList::trusted (PublicKey const& identity) const
{
    auto const signingKey = manifests_.getSigningKey (identity);
    auto const pubKey = signingKey ? *signingKey : identity;
    return validators_.find (pubKey) != validators_.end();
}

bool
ValidatorList::trustedPublisher (PublicKey const& identity) const
{
    return publisher_lists_.count (identity);
}

void
ValidatorList::setTimer ()
{
    auto next = sites_.end();

    for(auto it = sites_.begin (); it != sites_.end (); ++it)
        if(next == sites_.end () || it->nextRefresh < next->nextRefresh)
            next = it;

    if (next != sites_.end ())
    {
        timer_.expires_at (next->nextRefresh);
        timer_.async_wait (std::bind (&ValidatorList::onTimer, this,
            std::distance (sites_.begin (), next),
                beast::asio::placeholders::error));
    }
}

void
ValidatorList::onTimer (
    std::size_t siteIdx,
    error_code const& ec)
{
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
    {
        JLOG(j_.error()) <<
            "ValidatorList::onTimer: " << ec.message();
        return;
    }

    sites_[siteIdx].nextRefresh =
        clock_type::now() + sites_[siteIdx].refreshInterval;

    assert(! pending_);
    {
        std::lock_guard<std::mutex> lock{mutex_};
        pending_ = true;
    }

    std::shared_ptr<detail::Work> sp;
    if (sites_[siteIdx].pUrl.scheme == "https")
    {
        sp = std::make_shared<detail::WorkSSL>(
            sites_[siteIdx].pUrl.domain,
            sites_[siteIdx].pUrl.path,
            std::to_string(*sites_[siteIdx].pUrl.port),
            io_service_,
            std::bind(
                &ValidatorList::onWork,
                this,
                beast::asio::placeholders::error,
                std::placeholders::_2,
                siteIdx));
        work_ = sp;
    }
    else
    {
        sp = std::make_shared<detail::WorkPlain>(
            sites_[siteIdx].pUrl.domain,
            sites_[siteIdx].pUrl.path,
            std::to_string(*sites_[siteIdx].pUrl.port),
            io_service_,
            std::bind(
                &ValidatorList::onWork,
                this,
                beast::asio::placeholders::error,
                std::placeholders::_2,
                siteIdx));
        work_ = sp;
    }

    work_ = sp;
    sp->run ();
}

int
ValidatorList::getTargetSize () const
{
    if (fetchedLists_ == 0)
        return 0;

    // If only one list is available, use all available validators in that list
    if (fetchedLists_ == 1)
    {
        for (auto const& list : publisher_lists_)
        {
            if (list.second.list.size())
                return list.second.list.size();
        }
    }

    // Target trusted validator size is
    // 80% of the median recommended validator list size
    std::vector<int> listSizes;
    listSizes.reserve (publisher_lists_.size ());
    for (auto const& pubList : publisher_lists_)
    {
        if (! pubList.second.list.empty())
            listSizes.push_back (pubList.second.list.size());
    }

    std::sort (listSizes.begin(), listSizes.end());

    auto iter = std::next(listSizes.begin (), listSizes.size()/2);
    auto medianSize = *iter;
    if ((listSizes.size () % 2) == 0)
        medianSize = (medianSize + (*--iter)) / 2;

    return medianSize * 0.8;
}

int
ValidatorList::calcQuorum (
    std::uint32_t const& nTrusted,
    std::uint32_t const& nListed)
{
    const std::vector<int> neededValidations{
        0, 1, 2, 3, 3, 4, 5, 5, 6, 7, 7};
    auto constexpr quorumRatio = 0.8;

    // Floor of 80% of minimum required overlap with all listed validators
    auto quorum = std::ceil (
        quorumRatio *
        std::ceil (
            MIN_NETWORK_VALIDATOR_LIST_OVERLAP *
            std::max (nListed, nTrusted)));

    // If >10 trusted validators, set quorum to 80%
    // of validations from previous ledger.
    // Otherwise, use lookup table with quorums for
    // byzantine fault tolerance (3f+1)
    int nVals = (nTrusted > 10) ?
        std::ceil (nTrusted * quorumRatio) :
        neededValidations[nTrusted];

    return (quorum < nVals) ? nVals : quorum;
}

// should this not count our own validator key?
void
ValidatorList::update (
    ValidationSet const& activeValidators)
{
    std::lock_guard <std::mutex> lock{mutex_};

    std::multimap<int, PublicKey> rankedValidators;

    // "Iterate" the listed validators in random order so that it is not
    // deterministic which validators tied with the same number of listings
    // make the cut to be included on the trusted list
    std::vector<int> indexes;
    indexes.reserve (validatorListings_.size());
    for (int i=0; i<validatorListings_.size(); ++i)
        indexes.push_back (i);

    std::random_shuffle (indexes.begin(), indexes.end());
    for (auto const& index : indexes)
    {
        auto const& val = std::next (validatorListings_.begin(), index);

        if (manifests_.revoked (val->first))
            continue;

        // Check if this is a master public key with a known ephemeral key
        auto const ephKey = manifests_.getSigningKey (val->first);
        auto const valKey = ephKey ? *ephKey : val->first;

        // Do not use validators whose validations are missing
        if (activeValidators.find (calcNodeID (valKey)) !=
            activeValidators.end ())
        {
            rankedValidators.insert (
                std::pair<int,PublicKey>(val->second, valKey));
        }
    }

    JLOG (j_.debug()) <<
        rankedValidators.size () <<
        " validators eligible for inclusion in the trusted set";

    if (rankedValidators.size () <=
        std::ceil(validatorListings_.size () *
            MIN_NETWORK_VALIDATOR_LIST_OVERLAP))
    {
        JLOG (j_.warn()) <<
            "Less than " << MIN_NETWORK_VALIDATOR_LIST_OVERLAP <<
            " of the " << validatorListings_.size () <<
            " listed validators are eligible for inclusion in the trusted set";
    }

    auto const targetSize = getTargetSize ();
    if (targetSize < rankedValidators.size ())
    {
        rankedValidators.erase (
            std::next (rankedValidators.begin (), targetSize),
            rankedValidators.end ());
    }

    quorum_ = calcQuorum (
        rankedValidators.size (), validatorListings_.size ());

    if (rankedValidators.size() < quorum_)
    {
        JLOG (j_.warn()) <<
            "New quorum of " << quorum_ <<
            " exceeds the number of trusted validators (" <<
            rankedValidators.size() << ")";
    }

    validators_.clear ();
    for (auto const& val : rankedValidators)
        validators_.insert (val.second);
}

bool
ValidatorList::removeList (PublicKey const& publisherKey)
{
    std::lock_guard <std::mutex> lock{mutex_};

    auto const iList = publisher_lists_.find (publisherKey);
    if (iList == publisher_lists_.end ())
        return false;

    JLOG (j_.debug()) <<
        "Removing validator list for revoked publisher " <<
        toBase58(TokenType::TOKEN_NODE_PUBLIC, publisherKey);

    for (auto const& val : iList->second.list)
    {
        auto const& iVal = validatorListings_.find (val);
        if (iVal == validatorListings_.end())
            continue;

        if (iVal->second <= 1)
            validatorListings_.erase (iVal);
        else
            --iVal->second;
    }

    publisher_lists_.erase (iList);
    return true;
}

void
ValidatorList::for_each_listed (
    std::function<void(PublicKey const&, bool)> func) const
{
    std::lock_guard <std::mutex> lock{mutex_};

    for (auto const& v : validatorListings_)
        func (v.first, trusted(v.first));
}

bool
ValidatorList::load (
    PublicKey const& localSigningKey,
    std::vector<std::string> const& configValidators,
    std::vector<std::string> const& validatorListSites,
    std::vector<std::string> const& validatorListKeys,
    std::vector<std::string> const& configManifest)
{
    static boost::regex const re (
        "[[:space:]]*"            // skip leading whitespace
        "([[:alnum:]]+)"          // node identity
        "(?:"                     // begin optional comment block
        "[[:space:]]+"            // (skip all leading whitespace)
        "(?:"                     // begin optional comment
        "(.*[^[:space:]]+)"       // the comment
        "[[:space:]]*"            // (skip all trailing whitespace)
        ")?"                      // end optional comment
        ")?"                      // end optional comment block
    );

    JLOG (j_.debug()) <<
        "Loading configured validator list sites";

    std::lock_guard <std::mutex> lock{mutex_};
    for (auto uri : validatorListSites)
    {
        if (uri.find ("://") == std::string::npos)
            uri.insert (0, "https://");

        parsedURL pUrl;
        if (! parseUrl (pUrl, uri) ||
            (pUrl.scheme != "http" && pUrl.scheme != "https"))
        {
            JLOG (j_.error()) <<
                "Invalid validator site uri: " << uri;
            return false;
        }

        if (! pUrl.port)
            pUrl.port = (pUrl.scheme == "https") ? 443 : 80;

        sites_.push_back ({
            uri, pUrl,
            DEFAULT_REFRESH_INTERVAL, clock_type::now(), false});
    }

    JLOG (j_.debug()) <<
        "Loaded " << validatorListSites.size() << " sites";

    JLOG (j_.debug()) <<
        "Loading configured trusted validator list signing keys";

    std::size_t count = 0;
    for (auto key : validatorListKeys)
    {
        JLOG (j_.trace()) <<
            "Processing '" << key << "'";

        auto const id = parseBase58<PublicKey>(
            TokenType::TOKEN_ACCOUNT_PUBLIC, key);

        if (!id)
        {
            JLOG (j_.error()) <<
                "Invalid validator list signing key: " << key;
            return false;
        }

        if (publicKeyType(*id) != KeyType::ed25519)
        {
            JLOG (j_.error()) <<
                "Validator list signing key not using Ed25519: " << key;
            return false;
        }

        if (publisher_lists_.count(*id))
        {
            JLOG (j_.warn()) <<
                "Duplicate validator list signing key: " << key;
            continue;
        }
        publisher_lists_[*id];
        ++count;
    }

    JLOG (j_.debug()) <<
        "Loaded " << count << " keys";

    PublicKey localValidatorKey = localSigningKey;
    boost::optional<Manifest> mo;
    if (! configManifest.empty())
    {
        std::string s;
        s.reserve (188);
        for (auto const& line : configManifest)
            s += beast::rfc2616::trim(line);

        if (mo = make_Manifest (beast::detail::base64_decode(s)))
        {
            if (mo->signingKey != localSigningKey)
            {
                JLOG (j_.error()) <<
                    "Configured manifest's signing public key does not " <<
                    "match configured validation seed";
                return false;
            }

            if (mo->revoked())
            {
                JLOG (j_.error()) <<
                    "Configured manifest revokes validation signing public key";
                return false;
            }

            localValidatorKey = mo->masterKey;
        }
        else
        {
            JLOG (j_.error()) << "Malformed manifest in config";
            return false;
        }
    }
    else
    {
        JLOG (j_.debug()) << "No validation manifest in config";
    }

    // Skew local validator's list count to guarantee it is always trusted
    if (localValidatorKey.size())
    {
        validatorListings_.insert ({
            localValidatorKey,
            std::numeric_limits<std::size_t>::max() -
                publisher_lists_.size()});
    }

    if (mo &&
        manifests_.applyManifest (std::move(*mo), *this) !=
            ManifestDisposition::accepted)
    {
        JLOG (j_.error()) << "Validation manifest in config was rejected";
        return false;
    }

    JLOG (j_.debug()) <<
        "Loading configured validators";

    count = 0;
    PublicKey local;
    for (auto const& n : configValidators)
    {
        JLOG (j_.trace()) <<
            "Processing '" << n << "'";

        boost::smatch match;

        if (!boost::regex_match (n, match, re))
        {
            JLOG (j_.error()) <<
                "Malformed entry: '" << n << "'";
            return false;
        }

        auto const id = parseBase58<PublicKey>(
            TokenType::TOKEN_NODE_PUBLIC, match[1]);

        if (!id)
        {
            JLOG (j_.error()) << "Invalid node identity: " << match[1];
            return false;
        }

        // Skip local validator keys which were already added
        if (*id == localValidatorKey || *id == localSigningKey)
            continue;

        auto ret = validatorListings_.insert ({*id, 1});
        if (! ret.second)
        {
            JLOG (j_.warn()) << "Duplicate node identity: " << match[1];
            continue;
        }
        publisher_lists_[local].list.emplace_back (std::move(*id));
        ++count;
    }

    // Add local key to the local list if other validators
    // were listed in the config.

    // Do not have a list representing the validators listed in the config
    // if it only consists of the local validator and there are other lists
    // that can be fetched.
    if (count)
    {
        ++fetchedLists_;
        if (localValidatorKey.size())
            publisher_lists_[local].list.push_back (localValidatorKey);
    }
    else if (publisher_lists_.empty() && localValidatorKey.size())
    {
        ++fetchedLists_;
        publisher_lists_[local].list.push_back (localValidatorKey);
    }

    JLOG (j_.debug()) <<
        "Loaded " << count << " entries";

    if (! timer_.expires_at().time_since_epoch().count())
        setTimer ();

    return true;
}

} // ripple
