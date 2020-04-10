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

#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/basics/FileUtilities.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/base64.h>
#include <ripple/json/json_reader.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/messages.h>
#include <boost/regex.hpp>

#include <date/date.h>

#include <cmath>
#include <mutex>
#include <shared_mutex>

namespace ripple {

std::string
to_string(ListDisposition disposition)
{
    switch (disposition)
    {
        case ListDisposition::accepted:
            return "accepted";
        case ListDisposition::same_sequence:
            return "same_sequence";
        case ListDisposition::unsupported_version:
            return "unsupported_version";
        case ListDisposition::untrusted:
            return "untrusted";
        case ListDisposition::stale:
            return "stale";
        case ListDisposition::invalid:
            return "invalid";
    }
    return "unknown";
}

const std::string ValidatorList::filePrefix_ = "cache.";

ValidatorList::ValidatorList(
    ManifestCache& validatorManifests,
    ManifestCache& publisherManifests,
    TimeKeeper& timeKeeper,
    std::string const& databasePath,
    beast::Journal j,
    boost::optional<std::size_t> minimumQuorum)
    : validatorManifests_(validatorManifests)
    , publisherManifests_(publisherManifests)
    , timeKeeper_(timeKeeper)
    , dataPath_(databasePath)
    , j_(j)
    , quorum_(minimumQuorum.value_or(1))  // Genesis ledger quorum
    , minimumQuorum_(minimumQuorum)
{
}

bool
ValidatorList::load(
    PublicKey const& localSigningKey,
    std::vector<std::string> const& configKeys,
    std::vector<std::string> const& publisherKeys)
{
    static boost::regex const re(
        "[[:space:]]*"       // skip leading whitespace
        "([[:alnum:]]+)"     // node identity
        "(?:"                // begin optional comment block
        "[[:space:]]+"       // (skip all leading whitespace)
        "(?:"                // begin optional comment
        "(.*[^[:space:]]+)"  // the comment
        "[[:space:]]*"       // (skip all trailing whitespace)
        ")?"                 // end optional comment
        ")?"                 // end optional comment block
    );

    std::unique_lock<std::shared_timed_mutex> read_lock{mutex_};

    JLOG(j_.debug())
        << "Loading configured trusted validator list publisher keys";

    std::size_t count = 0;
    for (auto key : publisherKeys)
    {
        JLOG(j_.trace()) << "Processing '" << key << "'";

        auto const ret = strUnHex(key);

        if (!ret || !publicKeyType(makeSlice(*ret)))
        {
            JLOG(j_.error()) << "Invalid validator list publisher key: " << key;
            return false;
        }

        auto id = PublicKey(makeSlice(*ret));

        if (publisherManifests_.revoked(id))
        {
            JLOG(j_.warn())
                << "Configured validator list publisher key is revoked: "
                << key;
            continue;
        }

        if (publisherLists_.count(id))
        {
            JLOG(j_.warn())
                << "Duplicate validator list publisher key: " << key;
            continue;
        }

        publisherLists_[id].available = false;
        ++count;
    }

    JLOG(j_.debug()) << "Loaded " << count << " keys";

    localPubKey_ = validatorManifests_.getMasterKey(localSigningKey);

    // Treat local validator key as though it was listed in the config
    if (localPubKey_.size())
        keyListings_.insert({localPubKey_, 1});

    JLOG(j_.debug()) << "Loading configured validator keys";

    count = 0;
    PublicKey local;
    for (auto const& n : configKeys)
    {
        JLOG(j_.trace()) << "Processing '" << n << "'";

        boost::smatch match;

        if (!boost::regex_match(n, match, re))
        {
            JLOG(j_.error()) << "Malformed entry: '" << n << "'";
            return false;
        }

        auto const id = parseBase58<PublicKey>(TokenType::NodePublic, match[1]);

        if (!id)
        {
            JLOG(j_.error()) << "Invalid node identity: " << match[1];
            return false;
        }

        // Skip local key which was already added
        if (*id == localPubKey_ || *id == localSigningKey)
            continue;

        auto ret = keyListings_.insert({*id, 1});
        if (!ret.second)
        {
            JLOG(j_.warn()) << "Duplicate node identity: " << match[1];
            continue;
        }
        auto it = publisherLists_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(local),
            std::forward_as_tuple());
        // Config listed keys never expire
        if (it.second)
            it.first->second.expiration = TimeKeeper::time_point::max();
        it.first->second.list.emplace_back(*id);
        it.first->second.available = true;
        ++count;
    }

    JLOG(j_.debug()) << "Loaded " << count << " entries";

    return true;
}

boost::filesystem::path
ValidatorList::GetCacheFileName(PublicKey const& pubKey)
{
    return dataPath_ / (filePrefix_ + strHex(pubKey));
}

void
ValidatorList::CacheValidatorFile(
    PublicKey const& pubKey,
    PublisherList const& publisher)
{
    if (dataPath_.empty())
        return;

    boost::filesystem::path const filename = GetCacheFileName(pubKey);

    boost::system::error_code ec;

    Json::Value value(Json::objectValue);

    value["manifest"] = publisher.rawManifest;
    value["blob"] = publisher.rawBlob;
    value["signature"] = publisher.rawSignature;
    value["version"] = publisher.rawVersion;

    writeFileContents(ec, filename, value.toStyledString());

    if (ec)
    {
        // Log and ignore any file I/O exceptions
        JLOG(j_.error()) << "Problem writing " << filename << " " << ec.value()
                         << ": " << ec.message();
    }
}

ValidatorList::PublisherListStats
ValidatorList::applyListAndBroadcast(
    std::string const& manifest,
    std::string const& blob,
    std::string const& signature,
    std::uint32_t version,
    std::string siteUri,
    uint256 const& hash,
    Overlay& overlay,
    HashRouter& hashRouter)
{
    auto const result =
        applyList(manifest, blob, signature, version, std::move(siteUri), hash);
    auto const disposition = result.disposition;

    bool broadcast = disposition == ListDisposition::accepted ||
        disposition == ListDisposition::same_sequence;

    if (broadcast)
    {
        assert(result.available && result.publisherKey && result.sequence);
        auto const toSkip = hashRouter.shouldRelay(hash);

        if (toSkip)
        {
            protocol::TMValidatorList msg;
            msg.set_manifest(manifest);
            msg.set_blob(blob);
            msg.set_signature(signature);
            msg.set_version(version);

            auto const& publisherKey = *result.publisherKey;
            auto const sequence = *result.sequence;

            // Can't use overlay.foreach here because we need to modify
            // the peer, and foreach provides a const&
            auto message =
                std::make_shared<Message>(msg, protocol::mtVALIDATORLIST);
            for (auto& peer : overlay.getActivePeers())
            {
                if (toSkip->count(peer->id()) == 0 &&
                    peer->supportsFeature(
                        ProtocolFeature::ValidatorListPropagation) &&
                    peer->publisherListSequence(publisherKey) < sequence)
                {
                    peer->send(message);

                    JLOG(j_.debug())
                        << "Sent validator list for " << strHex(publisherKey)
                        << " with sequence " << sequence << " to "
                        << peer->getRemoteAddress().to_string() << " ("
                        << peer->id() << ")";
                    // Don't send it next time.
                    hashRouter.addSuppressionPeer(hash, peer->id());
                    peer->setPublisherListSequence(publisherKey, sequence);
                }
            }
        }
    }

    return result;
}

ValidatorList::PublisherListStats
ValidatorList::applyList(
    std::string const& manifest,
    std::string const& blob,
    std::string const& signature,
    std::uint32_t version,
    std::string siteUri,
    boost::optional<uint256> const& hash)
{
    using namespace std::string_literals;

    if (version != requiredListVersion)
        return PublisherListStats{ListDisposition::unsupported_version};

    std::unique_lock<std::shared_timed_mutex> lock{mutex_};

    Json::Value list;
    PublicKey pubKey;
    auto const result = verify(list, pubKey, manifest, blob, signature);
    if (result != ListDisposition::accepted)
    {
        if (result == ListDisposition::same_sequence &&
            publisherLists_.count(pubKey))
        {
            // We've seen this valid list already, so return
            // what we know about it.
            auto const& publisher = publisherLists_[pubKey];
            return PublisherListStats{
                result, pubKey, publisher.available, publisher.sequence};
        }
        return PublisherListStats{result};
    }

    // Update publisher's list
    Json::Value const& newList = list["validators"];
    auto& publisher = publisherLists_[pubKey];
    publisher.available = true;
    publisher.sequence = list["sequence"].asUInt();
    publisher.expiration = TimeKeeper::time_point{
        TimeKeeper::duration{list["expiration"].asUInt()}};
    publisher.siteUri = std::move(siteUri);
    publisher.rawManifest = manifest;
    publisher.rawBlob = blob;
    publisher.rawSignature = signature;
    publisher.rawVersion = version;
    if (hash)
        publisher.hash = *hash;
    std::vector<PublicKey>& publisherList = publisher.list;

    PublisherListStats const applyResult{
        result, pubKey, publisher.available, publisher.sequence};

    std::vector<PublicKey> oldList = publisherList;
    publisherList.clear();
    publisherList.reserve(newList.size());
    std::vector<std::string> manifests;
    for (auto const& val : newList)
    {
        if (val.isObject() && val.isMember("validation_public_key") &&
            val["validation_public_key"].isString())
        {
            boost::optional<Blob> const ret =
                strUnHex(val["validation_public_key"].asString());

            if (!ret || !publicKeyType(makeSlice(*ret)))
            {
                JLOG(j_.error()) << "Invalid node identity: "
                                 << val["validation_public_key"].asString();
            }
            else
            {
                publisherList.push_back(
                    PublicKey(Slice{ret->data(), ret->size()}));
            }

            if (val.isMember("manifest") && val["manifest"].isString())
                manifests.push_back(val["manifest"].asString());
        }
    }

    // Update keyListings_ for added and removed keys
    std::sort(publisherList.begin(), publisherList.end());

    auto iNew = publisherList.begin();
    auto iOld = oldList.begin();
    while (iNew != publisherList.end() || iOld != oldList.end())
    {
        if (iOld == oldList.end() ||
            (iNew != publisherList.end() && *iNew < *iOld))
        {
            // Increment list count for added keys
            ++keyListings_[*iNew];
            ++iNew;
        }
        else if (
            iNew == publisherList.end() ||
            (iOld != oldList.end() && *iOld < *iNew))
        {
            // Decrement list count for removed keys
            if (keyListings_[*iOld] <= 1)
                keyListings_.erase(*iOld);
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
        JLOG(j_.warn()) << "No validator keys included in valid list";
    }

    for (auto const& valManifest : manifests)
    {
        auto m = deserializeManifest(base64_decode(valManifest));

        if (!m || !keyListings_.count(m->masterKey))
        {
            JLOG(j_.warn()) << "List for " << strHex(pubKey)
                            << " contained untrusted validator manifest";
            continue;
        }

        if (auto const r = validatorManifests_.applyManifest(std::move(*m));
            r == ManifestDisposition::invalid)
        {
            JLOG(j_.warn()) << "List for " << strHex(pubKey)
                            << " contained invalid validator manifest";
        }
    }

    // Cache the validator list in a file
    CacheValidatorFile(pubKey, publisher);

    return applyResult;
}

std::vector<std::string>
ValidatorList::loadLists()
{
    using namespace std::string_literals;
    using namespace boost::filesystem;
    using namespace boost::system::errc;

    std::unique_lock<std::shared_timed_mutex> lock{mutex_};

    std::vector<std::string> sites;
    sites.reserve(publisherLists_.size());
    for (auto const& [pubKey, publisher] : publisherLists_)
    {
        boost::system::error_code ec;

        if (publisher.available)
            continue;

        boost::filesystem::path const filename = GetCacheFileName(pubKey);

        auto const fullPath{canonical(filename, ec)};
        if (ec)
            continue;

        auto size = file_size(fullPath, ec);
        if (!ec && !size)
        {
            // Treat an empty file as a missing file, because
            // nobody else is going to write it.
            ec = make_error_code(no_such_file_or_directory);
        }
        if (ec)
            continue;

        std::string const prefix = [&fullPath]() {
#if _MSC_VER  // MSVC: Windows paths need a leading / added
            {
                return fullPath.root_path() == "/"s ? "file://" : "file:///";
            }
#else
            {
                (void)fullPath;
                return "file://";
            }
#endif
        }();
        sites.emplace_back(prefix + fullPath.string());
    }

    // Then let the ValidatorSites do the rest of the work.
    return sites;
}

ListDisposition
ValidatorList::verify(
    Json::Value& list,
    PublicKey& pubKey,
    std::string const& manifest,
    std::string const& blob,
    std::string const& signature)
{
    auto m = deserializeManifest(base64_decode(manifest));

    if (!m || !publisherLists_.count(m->masterKey))
        return ListDisposition::untrusted;

    pubKey = m->masterKey;
    auto const revoked = m->revoked();

    auto const result = publisherManifests_.applyManifest(std::move(*m));

    if (revoked && result == ManifestDisposition::accepted)
    {
        removePublisherList(pubKey);
        publisherLists_.erase(pubKey);
    }

    if (revoked || result == ManifestDisposition::invalid)
        return ListDisposition::untrusted;

    auto const sig = strUnHex(signature);
    auto const data = base64_decode(blob);
    if (!sig ||
        !ripple::verify(
            publisherManifests_.getSigningKey(pubKey),
            makeSlice(data),
            makeSlice(*sig)))
        return ListDisposition::invalid;

    Json::Reader r;
    if (!r.parse(data, list))
        return ListDisposition::invalid;

    if (list.isMember("sequence") && list["sequence"].isInt() &&
        list.isMember("expiration") && list["expiration"].isInt() &&
        list.isMember("validators") && list["validators"].isArray())
    {
        auto const sequence = list["sequence"].asUInt();
        auto const expiration = TimeKeeper::time_point{
            TimeKeeper::duration{list["expiration"].asUInt()}};
        if (sequence < publisherLists_[pubKey].sequence ||
            expiration <= timeKeeper_.now())
            return ListDisposition::stale;
        else if (sequence == publisherLists_[pubKey].sequence)
            return ListDisposition::same_sequence;
    }
    else
    {
        return ListDisposition::invalid;
    }

    return ListDisposition::accepted;
}

bool
ValidatorList::listed(PublicKey const& identity) const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey(identity);
    return keyListings_.find(pubKey) != keyListings_.end();
}

bool
ValidatorList::trusted(PublicKey const& identity) const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey(identity);
    return trustedMasterKeys_.find(pubKey) != trustedMasterKeys_.end();
}

boost::optional<PublicKey>
ValidatorList::getListedKey(PublicKey const& identity) const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey(identity);
    if (keyListings_.find(pubKey) != keyListings_.end())
        return pubKey;
    return boost::none;
}

boost::optional<PublicKey>
ValidatorList::getTrustedKey(PublicKey const& identity) const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey(identity);
    if (trustedMasterKeys_.find(pubKey) != trustedMasterKeys_.end())
        return pubKey;
    return boost::none;
}

bool
ValidatorList::trustedPublisher(PublicKey const& identity) const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};
    return identity.size() && publisherLists_.count(identity);
}

PublicKey
ValidatorList::localPublicKey() const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};
    return localPubKey_;
}

bool
ValidatorList::removePublisherList(PublicKey const& publisherKey)
{
    auto const iList = publisherLists_.find(publisherKey);
    if (iList == publisherLists_.end())
        return false;

    JLOG(j_.debug()) << "Removing validator list for publisher "
                     << strHex(publisherKey);

    for (auto const& val : iList->second.list)
    {
        auto const& iVal = keyListings_.find(val);
        if (iVal == keyListings_.end())
            continue;

        if (iVal->second <= 1)
            keyListings_.erase(iVal);
        else
            --iVal->second;
    }

    iList->second.list.clear();
    iList->second.available = false;

    return true;
}

std::size_t
ValidatorList::count() const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};
    return publisherLists_.size();
}

boost::optional<TimeKeeper::time_point>
ValidatorList::expires() const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};
    boost::optional<TimeKeeper::time_point> res{boost::none};
    for (auto const& p : publisherLists_)
    {
        // Unfetched
        if (p.second.expiration == TimeKeeper::time_point{})
            return boost::none;

        // Earliest
        if (!res || p.second.expiration < *res)
            res = p.second.expiration;
    }
    return res;
}

Json::Value
ValidatorList::getJson() const
{
    Json::Value res(Json::objectValue);

    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};

    res[jss::validation_quorum] = static_cast<Json::UInt>(quorum());

    {
        auto& x = (res[jss::validator_list] = Json::objectValue);

        x[jss::count] = static_cast<Json::UInt>(count());

        if (auto when = expires())
        {
            if (*when == TimeKeeper::time_point::max())
            {
                x[jss::expiration] = "never";
                x[jss::status] = "active";
            }
            else
            {
                x[jss::expiration] = to_string(*when);

                if (*when > timeKeeper_.now())
                    x[jss::status] = "active";
                else
                    x[jss::status] = "expired";
            }
        }
        else
        {
            x[jss::status] = "unknown";
            x[jss::expiration] = "unknown";
        }
    }

    // Local static keys
    PublicKey local;
    Json::Value& jLocalStaticKeys =
        (res[jss::local_static_keys] = Json::arrayValue);
    if (auto it = publisherLists_.find(local); it != publisherLists_.end())
    {
        for (auto const& key : it->second.list)
            jLocalStaticKeys.append(toBase58(TokenType::NodePublic, key));
    }

    // Publisher lists
    Json::Value& jPublisherLists =
        (res[jss::publisher_lists] = Json::arrayValue);
    for (auto const& p : publisherLists_)
    {
        if (local == p.first)
            continue;
        Json::Value& curr = jPublisherLists.append(Json::objectValue);
        curr[jss::pubkey_publisher] = strHex(p.first);
        curr[jss::available] = p.second.available;
        curr[jss::uri] = p.second.siteUri;
        if (p.second.expiration != TimeKeeper::time_point{})
        {
            curr[jss::seq] = static_cast<Json::UInt>(p.second.sequence);
            curr[jss::expiration] = to_string(p.second.expiration);
            curr[jss::version] = requiredListVersion;
        }
        Json::Value& keys = (curr[jss::list] = Json::arrayValue);
        for (auto const& key : p.second.list)
        {
            keys.append(toBase58(TokenType::NodePublic, key));
        }
    }

    // Trusted validator keys
    Json::Value& jValidatorKeys =
        (res[jss::trusted_validator_keys] = Json::arrayValue);
    for (auto const& k : trustedMasterKeys_)
    {
        jValidatorKeys.append(toBase58(TokenType::NodePublic, k));
    }

    // signing keys
    Json::Value& jSigningKeys = (res[jss::signing_keys] = Json::objectValue);
    validatorManifests_.for_each_manifest([&jSigningKeys,
                                           this](Manifest const& manifest) {
        auto it = keyListings_.find(manifest.masterKey);
        if (it != keyListings_.end())
        {
            jSigningKeys[toBase58(TokenType::NodePublic, manifest.masterKey)] =
                toBase58(TokenType::NodePublic, manifest.signingKey);
        }
    });

    return res;
}

void
ValidatorList::for_each_listed(
    std::function<void(PublicKey const&, bool)> func) const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};

    for (auto const& v : keyListings_)
        func(v.first, trusted(v.first));
}

void
ValidatorList::for_each_available(std::function<void(
                                      std::string const& manifest,
                                      std::string const& blob,
                                      std::string const& signature,
                                      std::uint32_t version,
                                      PublicKey const& pubKey,
                                      std::size_t sequence,
                                      uint256 const& hash)> func) const
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};

    for (auto const& [key, pl] : publisherLists_)
    {
        if (!pl.available)
            continue;
        func(
            pl.rawManifest,
            pl.rawBlob,
            pl.rawSignature,
            pl.rawVersion,
            key,
            pl.sequence,
            pl.hash);
    }
}

boost::optional<Json::Value>
ValidatorList::getAvailable(boost::beast::string_view const& pubKey)
{
    std::shared_lock<std::shared_timed_mutex> read_lock{mutex_};

    auto const keyBlob = strViewUnHex(pubKey);

    if (!keyBlob || !publicKeyType(makeSlice(*keyBlob)))
    {
        JLOG(j_.info()) << "Invalid requested validator list publisher key: "
                        << pubKey;
        return {};
    }

    auto id = PublicKey(makeSlice(*keyBlob));

    auto iter = publisherLists_.find(id);

    if (iter == publisherLists_.end() || !iter->second.available)
        return {};

    Json::Value value(Json::objectValue);

    value["manifest"] = iter->second.rawManifest;
    value["blob"] = iter->second.rawBlob;
    value["signature"] = iter->second.rawSignature;
    value["version"] = iter->second.rawVersion;

    return value;
}

std::size_t
ValidatorList::calculateQuorum(std::size_t trusted, std::size_t seen)
{
    // Do not use achievable quorum until lists from all configured
    // publishers are available
    for (auto const& list : publisherLists_)
    {
        if (!list.second.available)
            return std::numeric_limits<std::size_t>::max();
    }

    // Use an 80% quorum to balance fork safety, liveness, and required UNL
    // overlap.
    //
    // Theorem 8 of the Analysis of the XRP Ledger Consensus Protocol
    // (https://arxiv.org/abs/1802.07242) says:
    //     XRP LCP guarantees fork safety if Oi,j > nj/2 + ni − qi + ti,j for
    //     every pair of nodes Pi, Pj.
    //
    // ni: size of Pi's UNL
    // nj: size of Pj's UNL
    // Oi,j: number of validators in both UNLs
    // qi: validation quorum for Pi's UNL
    // ti, tj: maximum number of allowed Byzantine faults in Pi and Pj's UNLs
    // ti,j: min{ti, tj, Oi,j}
    //
    // Assume ni < nj, meaning and ti,j = ti
    //
    // For qi = .8*ni, we make ti <= .2*ni
    // (We could make ti lower and tolerate less UNL overlap. However in order
    // to prioritize safety over liveness, we need ti >= ni - qi)
    //
    // An 80% quorum allows two UNLs to safely have < .2*ni unique validators
    // between them:
    //
    // pi = ni - Oi,j
    // pj = nj - Oi,j
    //
    // Oi,j > nj/2 + ni − qi + ti,j
    // ni - pi > (ni - pi + pj)/2 + ni − .8*ni + .2*ni
    // pi + pj < .2*ni
    auto quorum = static_cast<std::size_t>(std::ceil(trusted * 0.8f));

    // Use lower quorum specified via command line if the normal quorum appears
    // unreachable based on the number of recently received validations.
    if (minimumQuorum_ && *minimumQuorum_ < quorum && seen < quorum)
    {
        quorum = *minimumQuorum_;

        JLOG(j_.warn()) << "Using unsafe quorum of " << quorum
                        << " as specified in the command line";
    }

    return quorum;
}

TrustChanges
ValidatorList::updateTrusted(hash_set<NodeID> const& seenValidators)
{
    std::unique_lock<std::shared_timed_mutex> lock{mutex_};

    // Remove any expired published lists
    for (auto const& list : publisherLists_)
    {
        if (list.second.available &&
            list.second.expiration <= timeKeeper_.now())
            removePublisherList(list.first);
    }

    TrustChanges trustChanges;

    auto it = trustedMasterKeys_.cbegin();
    while (it != trustedMasterKeys_.cend())
    {
        if (!keyListings_.count(*it) || validatorManifests_.revoked(*it))
        {
            trustChanges.removed.insert(calcNodeID(*it));
            it = trustedMasterKeys_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto const& val : keyListings_)
    {
        if (!validatorManifests_.revoked(val.first) &&
            trustedMasterKeys_.emplace(val.first).second)
            trustChanges.added.insert(calcNodeID(val.first));
    }

    // If there were any changes, we need to update the ephemeral signing keys:
    if (!trustChanges.added.empty() || !trustChanges.removed.empty())
    {
        trustedSigningKeys_.clear();

        for (auto const& k : trustedMasterKeys_)
            trustedSigningKeys_.insert(validatorManifests_.getSigningKey(k));
    }

    JLOG(j_.debug())
        << trustedMasterKeys_.size() << "  of " << keyListings_.size()
        << " listed validators eligible for inclusion in the trusted set";

    quorum_ = calculateQuorum(trustedMasterKeys_.size(), seenValidators.size());

    JLOG(j_.debug()) << "Using quorum of " << quorum_ << " for new set of "
                     << trustedMasterKeys_.size() << " trusted validators ("
                     << trustChanges.added.size() << " added, "
                     << trustChanges.removed.size() << " removed)";

    if (trustedMasterKeys_.size() < quorum_)
    {
        JLOG(j_.warn()) << "New quorum of " << quorum_
                        << " exceeds the number of trusted validators ("
                        << trustedMasterKeys_.size() << ")";
    }

    return trustChanges;
}

}  // namespace ripple
