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

#include <ripple/app/misc/ValidatorList.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/json_reader.h>
#include <beast/core/detail/base64.hpp>
#include <boost/regex.hpp>

namespace ripple {

ValidatorList::ValidatorList (
    ManifestCache& validatorManifests,
    ManifestCache& publisherManifests,
    TimeKeeper& timeKeeper,
    beast::Journal j,
    boost::optional<std::size_t> minimumQuorum)
    : validatorManifests_ (validatorManifests)
    , publisherManifests_ (publisherManifests)
    , timeKeeper_ (timeKeeper)
    , j_ (j)
    , quorum_ (minimumQuorum.value_or(1)) // Genesis ledger quorum
    , minimumQuorum_ (minimumQuorum)
{
}

ValidatorList::~ValidatorList()
{
}

bool
ValidatorList::load (
    PublicKey const& localSigningKey,
    std::vector<std::string> const& configKeys,
    std::vector<std::string> const& publisherKeys)
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

    boost::unique_lock<boost::shared_mutex> read_lock{mutex_};

    JLOG (j_.debug()) <<
        "Loading configured trusted validator list publisher keys";

    std::size_t count = 0;
    for (auto key : publisherKeys)
    {
        JLOG (j_.trace()) <<
            "Processing '" << key << "'";

        auto const ret = strUnHex (key);

        if (! ret.second || ! ret.first.size ())
        {
            JLOG (j_.error()) <<
                "Invalid validator list publisher key: " << key;
            return false;
        }

        auto id = PublicKey(Slice{ ret.first.data (), ret.first.size() });

        if (validatorManifests_.revoked (id))
        {
            JLOG (j_.warn()) <<
                "Configured validator list publisher key is revoked: " << key;
            continue;
        }

        if (publisherLists_.count(id))
        {
            JLOG (j_.warn()) <<
                "Duplicate validator list publisher key: " << key;
            continue;
        }

        publisherLists_[id].available = false;
        ++count;
    }

    JLOG (j_.debug()) <<
        "Loaded " << count << " keys";

    localPubKey_ = validatorManifests_.getMasterKey (localSigningKey);

    // Treat local validator key as though it was listed in the config
    if (localPubKey_.size())
        keyListings_.insert ({ localPubKey_, 1 });

    JLOG (j_.debug()) <<
        "Loading configured validator keys";

    count = 0;
    PublicKey local;
    for (auto const& n : configKeys)
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

        // Skip local key which was already added
        if (*id == localPubKey_ || *id == localSigningKey)
            continue;

        auto ret = keyListings_.insert ({*id, 1});
        if (! ret.second)
        {
            JLOG (j_.warn()) << "Duplicate node identity: " << match[1];
            continue;
        }
        publisherLists_[local].list.emplace_back (std::move(*id));
        publisherLists_[local].available = true;
        ++count;
    }

    JLOG (j_.debug()) <<
        "Loaded " << count << " entries";

    return true;
}


ListDisposition
ValidatorList::applyList (
    std::string const& manifest,
    std::string const& blob,
    std::string const& signature,
    std::uint32_t version)
{
    if (version != 1)
        return ListDisposition::unsupported_version;

    boost::unique_lock<boost::shared_mutex> lock{mutex_};

    Json::Value list;
    PublicKey pubKey;
    auto const result = verify (list, pubKey, manifest, blob, signature);
    if (result != ListDisposition::accepted)
        return result;

    // Update publisher's list
    Json::Value const& newList = list["validators"];
    publisherLists_[pubKey].available = true;
    publisherLists_[pubKey].sequence = list["sequence"].asUInt ();
    publisherLists_[pubKey].expiration = list["expiration"].asUInt ();
    std::vector<PublicKey>& publisherList = publisherLists_[pubKey].list;

    std::vector<PublicKey> oldList = publisherList;
    publisherList.clear ();
    publisherList.reserve (newList.size ());
    for (auto const& val : newList)
    {
        if (val.isObject () &&
            val.isMember ("validation_public_key") &&
            val["validation_public_key"].isString ())
        {
            std::pair<Blob, bool> ret (strUnHex (
                val["validation_public_key"].asString ()));

            if (! ret.second || ! ret.first.size ())
            {
                JLOG (j_.error()) <<
                    "Invalid node identity: " <<
                    val["validation_public_key"].asString ();
            }
            else
            {
                publisherList.push_back (
                    PublicKey(Slice{ ret.first.data (), ret.first.size() }));
            }
        }
    }

    // Update keyListings_ for added and removed keys
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
            // Increment list count for added keys
            ++keyListings_[*iNew];
            ++iNew;
        }
        else if (iNew == publisherList.end () ||
            (iOld != oldList.end () && *iOld < *iNew))
        {
            // Decrement list count for removed keys
            if (keyListings_[*iOld] == 1)
                keyListings_.erase (*iOld);
            else
                --keyListings_[*iOld];
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
            "No validator keys included in valid list";
    }

    return ListDisposition::accepted;
}

ListDisposition
ValidatorList::verify (
    Json::Value& list,
    PublicKey& pubKey,
    std::string const& manifest,
    std::string const& blob,
    std::string const& signature)
{
    auto m = Manifest::make_Manifest (beast::detail::base64_decode(manifest));

    if (! m || ! publisherLists_.count (m->masterKey))
        return ListDisposition::untrusted;

    pubKey = m->masterKey;
    auto const revoked = m->revoked();

    auto const result = publisherManifests_.applyManifest (
        std::move(*m));

    if (revoked && result == ManifestDisposition::accepted)
    {
        removePublisherList (pubKey);
        publisherLists_.erase (pubKey);
    }

    if (revoked || result == ManifestDisposition::invalid)
        return ListDisposition::untrusted;

    auto const sig = strUnHex(signature);
    auto const data = beast::detail::base64_decode (blob);
    if (! sig.second ||
        ! ripple::verify (
            publisherManifests_.getSigningKey(pubKey),
            makeSlice(data),
            makeSlice(sig.first)))
        return ListDisposition::invalid;

    Json::Reader r;
    if (! r.parse (data, list))
        return ListDisposition::invalid;

    if (list.isMember("sequence") && list["sequence"].isInt() &&
        list.isMember("expiration") && list["expiration"].isInt() &&
        list.isMember("validators") && list["validators"].isArray())
    {
        auto const sequence = list["sequence"].asUInt ();
        auto const expiration = list["expiration"].asUInt ();
        if (sequence <= publisherLists_[pubKey].sequence ||
                expiration <= timeKeeper_.now().time_since_epoch().count())
            return ListDisposition::stale;
    }
    else
    {
        return ListDisposition::invalid;
    }

    return ListDisposition::accepted;
}

bool
ValidatorList::listed (
    PublicKey const& identity) const
{
    boost::shared_lock<boost::shared_mutex> read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey (identity);
    return keyListings_.find (pubKey) != keyListings_.end ();
}

bool
ValidatorList::trusted (PublicKey const& identity) const
{
    boost::shared_lock<boost::shared_mutex> read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey (identity);
    return trustedKeys_.find (pubKey) != trustedKeys_.end();
}

boost::optional<PublicKey>
ValidatorList::getListedKey (
    PublicKey const& identity) const
{
    boost::shared_lock<boost::shared_mutex> read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey (identity);
    if (keyListings_.find (pubKey) != keyListings_.end ())
        return pubKey;
    return boost::none;
}

boost::optional<PublicKey>
ValidatorList::getTrustedKey (PublicKey const& identity) const
{
    boost::shared_lock<boost::shared_mutex> read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey (identity);
    if (trustedKeys_.find (pubKey) != trustedKeys_.end())
        return pubKey;
    return boost::none;
}

bool
ValidatorList::trustedPublisher (PublicKey const& identity) const
{
    boost::shared_lock<boost::shared_mutex> read_lock{mutex_};
    return identity.size() && publisherLists_.count (identity);
}

PublicKey
ValidatorList::localPublicKey () const
{
    boost::shared_lock<boost::shared_mutex> read_lock{mutex_};
    return localPubKey_;
}

bool
ValidatorList::removePublisherList (PublicKey const& publisherKey)
{
    auto const iList = publisherLists_.find (publisherKey);
    if (iList == publisherLists_.end ())
        return false;

    JLOG (j_.debug()) <<
        "Removing validator list for revoked publisher " <<
        toBase58(TokenType::TOKEN_NODE_PUBLIC, publisherKey);

    for (auto const& val : iList->second.list)
    {
        auto const& iVal = keyListings_.find (val);
        if (iVal == keyListings_.end())
            continue;

        if (iVal->second <= 1)
            keyListings_.erase (iVal);
        else
            --iVal->second;
    }

    return true;
}

void
ValidatorList::for_each_listed (
    std::function<void(PublicKey const&, bool)> func) const
{
    boost::shared_lock<boost::shared_mutex> read_lock{mutex_};

    for (auto const& v : keyListings_)
        func (v.first, trusted(v.first));
}

std::size_t
ValidatorList::calculateMinimumQuorum (
    std::size_t nListedKeys, bool unlistedLocal)
{
    // Only require 51% quorum for small number of validators to facilitate
    // bootstrapping a network.
    if (nListedKeys <= 5)
        return nListedKeys/2 + 1;

    // The number of listed validators is increased to preserve the safety
    // guarantee for two unlisted validators using the same set of listed
    // validators.
    if (unlistedLocal)
        ++nListedKeys;

    // Guarantee safety with up to 1/3 listed validators being malicious.
    // This prioritizes safety (Byzantine fault tolerance) over liveness.
    // It takes at least as many malicious nodes to split/fork the network as
    // to stall the network.
    // At 67%, the overlap of two quorums is 34%
    //   67 + 67 - 100 = 34
    // So under certain conditions, 34% of validators could vote for two
    // different ledgers and split the network.
    // Similarly 34% could prevent quorum from being met (by not voting) and
    // stall the network.
    // If/when the quorum is subsequently raised to/towards 80%, it becomes
    // harder to split the network (more safe) and easier to stall it (less live).
    return nListedKeys * 2/3 + 1;
}

} // ripple
