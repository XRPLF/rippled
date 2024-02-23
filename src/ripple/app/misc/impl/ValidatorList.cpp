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
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/basics/FileUtilities.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/base64.h>
#include <ripple/json/json_reader.h>
#include <ripple/overlay/Overlay.h>
#include <ripple/protocol/STValidation.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/jss.h>
#include <ripple/protocol/messages.h>
#include <boost/regex.hpp>

#include <cmath>
#include <mutex>
#include <numeric>
#include <shared_mutex>

namespace ripple {

std::string
to_string(ListDisposition disposition)
{
    switch (disposition)
    {
        case ListDisposition::accepted:
            return "accepted";
        case ListDisposition::expired:
            return "expired";
        case ListDisposition::same_sequence:
            return "same_sequence";
        case ListDisposition::pending:
            return "pending";
        case ListDisposition::known_sequence:
            return "known_sequence";
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

ValidatorList::PublisherListStats::PublisherListStats(ListDisposition d)
{
    ++dispositions[d];
}

ValidatorList::PublisherListStats::PublisherListStats(
    ListDisposition d,
    PublicKey key,
    PublisherStatus stat,
    std::size_t seq)
    : publisherKey(key), status(stat), sequence(seq)
{
    ++dispositions[d];
}

ListDisposition
ValidatorList::PublisherListStats::bestDisposition() const
{
    return dispositions.empty() ? ListDisposition::invalid
                                : dispositions.begin()->first;
}

ListDisposition
ValidatorList::PublisherListStats::worstDisposition() const
{
    return dispositions.empty() ? ListDisposition::invalid
                                : dispositions.rbegin()->first;
}

void
ValidatorList::PublisherListStats::mergeDispositions(
    PublisherListStats const& src)
{
    for (auto const& [disp, count] : src.dispositions)
    {
        dispositions[disp] += count;
    }
}

ValidatorList::MessageWithHash::MessageWithHash(
    std::shared_ptr<Message> const& message_,
    uint256 hash_,
    std::size_t num_)
    : message(message_), hash(hash_), numVLs(num_)
{
}

const std::string ValidatorList::filePrefix_ = "cache.";

ValidatorList::ValidatorList(
    ManifestCache& validatorManifests,
    ManifestCache& publisherManifests,
    TimeKeeper& timeKeeper,
    std::string const& databasePath,
    beast::Journal j,
    std::optional<std::size_t> minimumQuorum)
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

    std::lock_guard lock{mutex_};

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
        auto status = PublisherStatus::unavailable;

        if (publisherManifests_.revoked(id))
        {
            JLOG(j_.warn())
                << "Configured validator list publisher key is revoked: "
                << key;
            status = PublisherStatus::revoked;
        }

        if (publisherLists_.count(id))
        {
            JLOG(j_.warn())
                << "Duplicate validator list publisher key: " << key;
            continue;
        }

        publisherLists_[id].status = status;
        ++count;
    }

    JLOG(j_.debug()) << "Loaded " << count << " keys";

    localPubKey_ = validatorManifests_.getMasterKey(localSigningKey);

    // Treat local validator key as though it was listed in the config
    if (localPubKey_.size())
        keyListings_.insert({localPubKey_, 1});

    JLOG(j_.debug()) << "Loading configured validator keys";

    count = 0;
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
        auto [it, inserted] = publisherLists_.emplace();
        // Config listed keys never expire
        auto& current = it->second.current;
        if (inserted)
            current.validUntil = TimeKeeper::time_point::max();
        current.list.emplace_back(*id);
        it->second.status = PublisherStatus::available;
        ++count;
    }

    JLOG(j_.debug()) << "Loaded " << count << " entries";

    return true;
}

boost::filesystem::path
ValidatorList::getCacheFileName(
    ValidatorList::lock_guard const&,
    PublicKey const& pubKey) const
{
    return dataPath_ / (filePrefix_ + strHex(pubKey));
}

// static
Json::Value
ValidatorList::buildFileData(
    std::string const& pubKey,
    ValidatorList::PublisherListCollection const& pubCollection,
    beast::Journal j)
{
    return buildFileData(pubKey, pubCollection, {}, j);
}

// static
Json::Value
ValidatorList::buildFileData(
    std::string const& pubKey,
    ValidatorList::PublisherListCollection const& pubCollection,
    std::optional<std::uint32_t> forceVersion,
    beast::Journal j)
{
    Json::Value value(Json::objectValue);

    assert(pubCollection.rawVersion == 2 || pubCollection.remaining.empty());
    auto const effectiveVersion =
        forceVersion ? *forceVersion : pubCollection.rawVersion;

    value[jss::manifest] = pubCollection.rawManifest;
    value[jss::version] = effectiveVersion;
    value[jss::public_key] = pubKey;

    switch (effectiveVersion)
    {
        case 1: {
            auto const& current = pubCollection.current;
            value[jss::blob] = current.rawBlob;
            value[jss::signature] = current.rawSignature;
            // This is only possible if "downgrading" a v2 UNL to v1, for
            // example for the /vl/ endpoint.
            if (current.rawManifest &&
                *current.rawManifest != pubCollection.rawManifest)
                value[jss::manifest] = *current.rawManifest;
            break;
        }
        case 2: {
            Json::Value blobs(Json::arrayValue);

            auto add = [&blobs, &outerManifest = pubCollection.rawManifest](
                           PublisherList const& pubList) {
                auto& blob = blobs.append(Json::objectValue);
                blob[jss::blob] = pubList.rawBlob;
                blob[jss::signature] = pubList.rawSignature;
                if (pubList.rawManifest &&
                    *pubList.rawManifest != outerManifest)
                    blob[jss::manifest] = *pubList.rawManifest;
            };

            add(pubCollection.current);
            for (auto const& [_, pending] : pubCollection.remaining)
            {
                (void)_;
                add(pending);
            }

            value[jss::blobs_v2] = std::move(blobs);
            break;
        }
        default:
            JLOG(j.trace())
                << "Invalid VL version provided: " << effectiveVersion;
            value = Json::nullValue;
    }

    return value;
}

void
ValidatorList::cacheValidatorFile(
    ValidatorList::lock_guard const& lock,
    PublicKey const& pubKey) const
{
    if (dataPath_.empty())
        return;

    boost::filesystem::path const filename = getCacheFileName(lock, pubKey);

    boost::system::error_code ec;

    Json::Value value =
        buildFileData(strHex(pubKey), publisherLists_.at(pubKey), j_);
    // rippled should be the only process writing to this file, so
    // if it ever needs to be read, it is not expected to change externally, so
    // delay the refresh as long as possible: 24 hours. (See also
    // `ValidatorSite::missingSite()`)
    value[jss::refresh_interval] = 24 * 60;

    writeFileContents(ec, filename, value.toStyledString());

    if (ec)
    {
        // Log and ignore any file I/O exceptions
        JLOG(j_.error()) << "Problem writing " << filename << " " << ec.value()
                         << ": " << ec.message();
    }
}

// static
std::vector<ValidatorBlobInfo>
ValidatorList::parseBlobs(std::uint32_t version, Json::Value const& body)
{
    std::vector<ValidatorBlobInfo> result;
    switch (version)
    {
        case 1: {
            if (!body.isMember(jss::blob) || !body[jss::blob].isString() ||
                !body.isMember(jss::signature) ||
                !body[jss::signature].isString() ||
                // If the v2 field is present, the VL is malformed
                body.isMember(jss::blobs_v2))
                return {};
            ValidatorBlobInfo& info = result.emplace_back();
            info.blob = body[jss::blob].asString();
            info.signature = body[jss::signature].asString();
            assert(result.size() == 1);
            return result;
        }
            // Treat unknown versions as if they're the latest version. This
            // will likely break a bunch of unit tests each time we introduce a
            // new version, so don't do it casually. Note that the version is
            // validated elsewhere.
        case 2:
        default: {
            if (!body.isMember(jss::blobs_v2) ||
                !body[jss::blobs_v2].isArray() ||
                body[jss::blobs_v2].size() > maxSupportedBlobs ||
                // If any of the v1 fields are present, the VL is malformed
                body.isMember(jss::blob) || body.isMember(jss::signature))
                return {};
            auto const& blobs = body[jss::blobs_v2];
            result.reserve(blobs.size());
            for (auto const& blobInfo : blobs)
            {
                if (!blobInfo.isObject() ||
                    !blobInfo.isMember(jss::signature) ||
                    !blobInfo[jss::signature].isString() ||
                    !blobInfo.isMember(jss::blob) ||
                    !blobInfo[jss::blob].isString())
                    return {};
                ValidatorBlobInfo& info = result.emplace_back();
                info.blob = blobInfo[jss::blob].asString();
                info.signature = blobInfo[jss::signature].asString();
                if (blobInfo.isMember(jss::manifest))
                {
                    if (!blobInfo[jss::manifest].isString())
                        return {};
                    info.manifest = blobInfo[jss::manifest].asString();
                }
            }
            assert(result.size() == blobs.size());
            return result;
        }
    }
}

// static
std::vector<ValidatorBlobInfo>
ValidatorList::parseBlobs(protocol::TMValidatorList const& body)
{
    return {{body.blob(), body.signature(), {}}};
}

// static
std::vector<ValidatorBlobInfo>
ValidatorList::parseBlobs(protocol::TMValidatorListCollection const& body)
{
    if (body.blobs_size() > maxSupportedBlobs)
        return {};
    std::vector<ValidatorBlobInfo> result;
    result.reserve(body.blobs_size());
    for (auto const& blob : body.blobs())
    {
        ValidatorBlobInfo& info = result.emplace_back();
        info.blob = blob.blob();
        info.signature = blob.signature();
        if (blob.has_manifest())
        {
            info.manifest = blob.manifest();
        }
    }
    assert(result.size() == body.blobs_size());
    return result;
}

std::size_t
splitMessageParts(
    std::vector<ValidatorList::MessageWithHash>& messages,
    protocol::TMValidatorListCollection const& largeMsg,
    std::size_t maxSize,
    std::size_t begin,
    std::size_t end);

std::size_t
splitMessage(
    std::vector<ValidatorList::MessageWithHash>& messages,
    protocol::TMValidatorListCollection const& largeMsg,
    std::size_t maxSize,
    std::size_t begin = 0,
    std::size_t end = 0)
{
    if (begin == 0 && end == 0)
        end = largeMsg.blobs_size();
    assert(begin < end);
    if (end <= begin)
        return 0;

    auto mid = (begin + end) / 2;
    // The parts function will do range checking
    // Use two separate calls to ensure deterministic order
    auto result = splitMessageParts(messages, largeMsg, maxSize, begin, mid);
    return result + splitMessageParts(messages, largeMsg, maxSize, mid, end);
}

std::size_t
splitMessageParts(
    std::vector<ValidatorList::MessageWithHash>& messages,
    protocol::TMValidatorListCollection const& largeMsg,
    std::size_t maxSize,
    std::size_t begin,
    std::size_t end)
{
    if (end <= begin)
        return 0;
    if (end - begin == 1)
    {
        protocol::TMValidatorList smallMsg;
        smallMsg.set_version(1);
        smallMsg.set_manifest(largeMsg.manifest());

        auto const& blob = largeMsg.blobs(begin);
        smallMsg.set_blob(blob.blob());
        smallMsg.set_signature(blob.signature());
        // This is only possible if "downgrading" a v2 UNL to v1.
        if (blob.has_manifest())
            smallMsg.set_manifest(blob.manifest());

        assert(Message::totalSize(smallMsg) <= maximiumMessageSize);

        messages.emplace_back(
            std::make_shared<Message>(smallMsg, protocol::mtVALIDATORLIST),
            sha512Half(smallMsg),
            1);
        return messages.back().numVLs;
    }
    else
    {
        std::optional<protocol::TMValidatorListCollection> smallMsg;
        smallMsg.emplace();
        smallMsg->set_version(largeMsg.version());
        smallMsg->set_manifest(largeMsg.manifest());

        for (std::size_t i = begin; i < end; ++i)
        {
            *smallMsg->add_blobs() = largeMsg.blobs(i);
        }

        if (Message::totalSize(*smallMsg) > maxSize)
        {
            // free up the message space
            smallMsg.reset();
            return splitMessage(messages, largeMsg, maxSize, begin, end);
        }
        else
        {
            messages.emplace_back(
                std::make_shared<Message>(
                    *smallMsg, protocol::mtVALIDATORLISTCOLLECTION),
                sha512Half(*smallMsg),
                smallMsg->blobs_size());
            return messages.back().numVLs;
        }
    }
    return 0;
}

// Build a v1 protocol message using only the current VL
std::size_t
buildValidatorListMessage(
    std::vector<ValidatorList::MessageWithHash>& messages,
    std::uint32_t rawVersion,
    std::string const& rawManifest,
    ValidatorBlobInfo const& currentBlob,
    std::size_t maxSize)
{
    assert(messages.empty());
    protocol::TMValidatorList msg;
    auto const manifest =
        currentBlob.manifest ? *currentBlob.manifest : rawManifest;
    auto const version = 1;
    msg.set_manifest(manifest);
    msg.set_blob(currentBlob.blob);
    msg.set_signature(currentBlob.signature);
    // Override the version
    msg.set_version(version);

    assert(Message::totalSize(msg) <= maximiumMessageSize);
    messages.emplace_back(
        std::make_shared<Message>(msg, protocol::mtVALIDATORLIST),
        sha512Half(msg),
        1);
    return 1;
}

// Build a v2 protocol message using all the VLs with sequence larger than the
// peer's
std::size_t
buildValidatorListMessage(
    std::vector<ValidatorList::MessageWithHash>& messages,
    std::uint64_t peerSequence,
    std::uint32_t rawVersion,
    std::string const& rawManifest,
    std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
    std::size_t maxSize)
{
    assert(messages.empty());
    protocol::TMValidatorListCollection msg;
    auto const version = rawVersion < 2 ? 2 : rawVersion;
    msg.set_version(version);
    msg.set_manifest(rawManifest);

    for (auto const& [sequence, blobInfo] : blobInfos)
    {
        if (sequence <= peerSequence)
            continue;
        protocol::ValidatorBlobInfo& blob = *msg.add_blobs();
        blob.set_blob(blobInfo.blob);
        blob.set_signature(blobInfo.signature);
        if (blobInfo.manifest)
            blob.set_manifest(*blobInfo.manifest);
    }
    assert(msg.blobs_size() > 0);
    if (Message::totalSize(msg) > maxSize)
    {
        // split into smaller messages
        return splitMessage(messages, msg, maxSize);
    }
    else
    {
        messages.emplace_back(
            std::make_shared<Message>(msg, protocol::mtVALIDATORLISTCOLLECTION),
            sha512Half(msg),
            msg.blobs_size());
        return messages.back().numVLs;
    }
}

[[nodiscard]]
// static
std::pair<std::size_t, std::size_t>
ValidatorList::buildValidatorListMessages(
    std::size_t messageVersion,
    std::uint64_t peerSequence,
    std::size_t maxSequence,
    std::uint32_t rawVersion,
    std::string const& rawManifest,
    std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
    std::vector<ValidatorList::MessageWithHash>& messages,
    std::size_t maxSize /*= maximiumMessageSize*/)
{
    assert(!blobInfos.empty());
    auto const& [currentSeq, currentBlob] = *blobInfos.begin();
    auto numVLs = std::accumulate(
        messages.begin(),
        messages.end(),
        0,
        [](std::size_t total, MessageWithHash const& m) {
            return total + m.numVLs;
        });
    if (messageVersion == 2 && peerSequence < maxSequence)
    {
        // Version 2
        if (messages.empty())
        {
            numVLs = buildValidatorListMessage(
                messages,
                peerSequence,
                rawVersion,
                rawManifest,
                blobInfos,
                maxSize);
            if (messages.empty())
                // No message was generated. Create an empty placeholder so we
                // dont' repeat the work later.
                messages.emplace_back();
        }

        // Don't send it next time.
        return {maxSequence, numVLs};
    }
    else if (messageVersion == 1 && peerSequence < currentSeq)
    {
        // Version 1
        if (messages.empty())
        {
            numVLs = buildValidatorListMessage(
                messages,
                rawVersion,
                currentBlob.manifest ? *currentBlob.manifest : rawManifest,
                currentBlob,
                maxSize);
            if (messages.empty())
                // No message was generated. Create an empty placeholder so we
                // dont' repeat the work later.
                messages.emplace_back();
        }

        // Don't send it next time.
        return {currentSeq, numVLs};
    }
    return {0, 0};
}

// static
void
ValidatorList::sendValidatorList(
    Peer& peer,
    std::uint64_t peerSequence,
    PublicKey const& publisherKey,
    std::size_t maxSequence,
    std::uint32_t rawVersion,
    std::string const& rawManifest,
    std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
    std::vector<ValidatorList::MessageWithHash>& messages,
    HashRouter& hashRouter,
    beast::Journal j)
{
    std::size_t const messageVersion =
        peer.supportsFeature(ProtocolFeature::ValidatorList2Propagation)
        ? 2
        : peer.supportsFeature(ProtocolFeature::ValidatorListPropagation) ? 1
                                                                          : 0;
    if (!messageVersion)
        return;
    auto const [newPeerSequence, numVLs] = buildValidatorListMessages(
        messageVersion,
        peerSequence,
        maxSequence,
        rawVersion,
        rawManifest,
        blobInfos,
        messages);
    if (newPeerSequence)
    {
        assert(!messages.empty());
        // Don't send it next time.
        peer.setPublisherListSequence(publisherKey, newPeerSequence);

        bool sent = false;
        for (auto const& message : messages)
        {
            if (message.message)
            {
                peer.send(message.message);
                hashRouter.addSuppressionPeer(message.hash, peer.id());
                sent = true;
            }
        }
        // The only way sent wil be false is if the messages was too big, and
        // thus there will only be one entry without a message
        assert(sent || messages.size() == 1);
        if (sent)
        {
            if (messageVersion > 1)
                JLOG(j.debug())
                    << "Sent " << messages.size()
                    << " validator list collection(s) containing " << numVLs
                    << " validator list(s) for " << strHex(publisherKey)
                    << " with sequence range " << peerSequence << ", "
                    << newPeerSequence << " to "
                    << peer.getRemoteAddress().to_string() << " [" << peer.id()
                    << "]";
            else
            {
                assert(numVLs == 1);
                JLOG(j.debug())
                    << "Sent validator list for " << strHex(publisherKey)
                    << " with sequence " << newPeerSequence << " to "
                    << peer.getRemoteAddress().to_string() << " [" << peer.id()
                    << "]";
            }
        }
    }
}

// static
void
ValidatorList::sendValidatorList(
    Peer& peer,
    std::uint64_t peerSequence,
    PublicKey const& publisherKey,
    std::size_t maxSequence,
    std::uint32_t rawVersion,
    std::string const& rawManifest,
    std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
    HashRouter& hashRouter,
    beast::Journal j)
{
    std::vector<ValidatorList::MessageWithHash> messages;
    sendValidatorList(
        peer,
        peerSequence,
        publisherKey,
        maxSequence,
        rawVersion,
        rawManifest,
        blobInfos,
        messages,
        hashRouter,
        j);
}

// static
void
ValidatorList::buildBlobInfos(
    std::map<std::size_t, ValidatorBlobInfo>& blobInfos,
    ValidatorList::PublisherListCollection const& lists)
{
    auto const& current = lists.current;
    auto const& remaining = lists.remaining;
    blobInfos[current.sequence] = {
        current.rawBlob, current.rawSignature, current.rawManifest};
    for (auto const& [sequence, vl] : remaining)
    {
        blobInfos[sequence] = {vl.rawBlob, vl.rawSignature, vl.rawManifest};
    }
}

// static
std::map<std::size_t, ValidatorBlobInfo>
ValidatorList::buildBlobInfos(
    ValidatorList::PublisherListCollection const& lists)
{
    std::map<std::size_t, ValidatorBlobInfo> result;
    buildBlobInfos(result, lists);
    return result;
}

// static
void
ValidatorList::broadcastBlobs(
    PublicKey const& publisherKey,
    ValidatorList::PublisherListCollection const& lists,
    std::size_t maxSequence,
    uint256 const& hash,
    Overlay& overlay,
    HashRouter& hashRouter,
    beast::Journal j)
{
    auto const toSkip = hashRouter.shouldRelay(hash);

    if (toSkip)
    {
        // We don't know what messages or message versions we're sending
        // until we examine our peer's properties. Build the message(s) on
        // demand, but reuse them when possible.

        // This will hold a v1 message with only the current VL if we have
        // any peers that don't support v2
        std::vector<ValidatorList::MessageWithHash> messages1;
        // This will hold v2 messages indexed by the peer's
        // `publisherListSequence`. For each `publisherListSequence`, we'll
        // only send the VLs with higher sequences.
        std::map<std::size_t, std::vector<ValidatorList::MessageWithHash>>
            messages2;
        // If any peers are found that are worth considering, this list will
        // be built to hold info for all of the valid VLs.
        std::map<std::size_t, ValidatorBlobInfo> blobInfos;

        assert(
            lists.current.sequence == maxSequence ||
            lists.remaining.count(maxSequence) == 1);
        // Can't use overlay.foreach here because we need to modify
        // the peer, and foreach provides a const&
        for (auto& peer : overlay.getActivePeers())
        {
            if (toSkip->count(peer->id()) == 0)
            {
                auto const peerSequence =
                    peer->publisherListSequence(publisherKey).value_or(0);
                if (peerSequence < maxSequence)
                {
                    if (blobInfos.empty())
                        buildBlobInfos(blobInfos, lists);
                    auto const v2 = peer->supportsFeature(
                        ProtocolFeature::ValidatorList2Propagation);
                    sendValidatorList(
                        *peer,
                        peerSequence,
                        publisherKey,
                        maxSequence,
                        lists.rawVersion,
                        lists.rawManifest,
                        blobInfos,
                        v2 ? messages2[peerSequence] : messages1,
                        hashRouter,
                        j);
                    // Even if the peer doesn't support the messages,
                    // suppress it so it'll be ignored next time.
                    hashRouter.addSuppressionPeer(hash, peer->id());
                }
            }
        }
    }
}

ValidatorList::PublisherListStats
ValidatorList::applyListsAndBroadcast(
    std::string const& manifest,
    std::uint32_t version,
    std::vector<ValidatorBlobInfo> const& blobs,
    std::string siteUri,
    uint256 const& hash,
    Overlay& overlay,
    HashRouter& hashRouter,
    NetworkOPs& networkOPs)
{
    auto const result =
        applyLists(manifest, version, blobs, std::move(siteUri), hash);
    auto const disposition = result.bestDisposition();

    if (disposition == ListDisposition::accepted)
    {
        bool good = true;
        for (auto const& [pubKey, listCollection] : publisherLists_)
        {
            (void)pubKey;
            if (listCollection.status != PublisherStatus::available)
            {
                good = false;
                break;
            }
        }
        if (good)
        {
            networkOPs.clearUNLBlocked();
        }
    }
    bool broadcast = disposition <= ListDisposition::known_sequence;

    if (broadcast)
    {
        auto const& pubCollection = publisherLists_[*result.publisherKey];
        assert(
            result.status <= PublisherStatus::expired && result.publisherKey &&
            pubCollection.maxSequence);
        broadcastBlobs(
            *result.publisherKey,
            pubCollection,
            *pubCollection.maxSequence,
            hash,
            overlay,
            hashRouter,
            j_);
    }

    return result;
}

ValidatorList::PublisherListStats
ValidatorList::applyLists(
    std::string const& manifest,
    std::uint32_t version,
    std::vector<ValidatorBlobInfo> const& blobs,
    std::string siteUri,
    std::optional<uint256> const& hash /* = {} */)
{
    if (std::count(
            std::begin(supportedListVersions),
            std::end(supportedListVersions),
            version) != 1)
        return PublisherListStats{ListDisposition::unsupported_version};

    std::lock_guard lock{mutex_};

    PublisherListStats result;
    for (auto const& blobInfo : blobs)
    {
        auto stats = applyList(
            manifest,
            blobInfo.manifest,
            blobInfo.blob,
            blobInfo.signature,
            version,
            siteUri,
            hash,
            lock);

        if (stats.bestDisposition() < result.bestDisposition() ||
            (stats.bestDisposition() == result.bestDisposition() &&
             stats.sequence > result.sequence))
        {
            stats.mergeDispositions(result);
            result = std::move(stats);
        }
        else
            result.mergeDispositions(stats);
        /////////
    }

    // Clean up the collection, because some of the processing may have made it
    // inconsistent
    if (result.publisherKey && publisherLists_.count(*result.publisherKey))
    {
        auto& pubCollection = publisherLists_[*result.publisherKey];
        auto& remaining = pubCollection.remaining;
        auto const& current = pubCollection.current;
        for (auto iter = remaining.begin(); iter != remaining.end();)
        {
            auto next = std::next(iter);
            assert(next == remaining.end() || next->first > iter->first);
            if (iter->first <= current.sequence ||
                (next != remaining.end() &&
                 next->second.validFrom <= iter->second.validFrom))
            {
                iter = remaining.erase(iter);
            }
            else
            {
                iter = next;
            }
        }

        cacheValidatorFile(lock, *result.publisherKey);

        pubCollection.fullHash = sha512Half(pubCollection);

        result.sequence = *pubCollection.maxSequence;
    }

    return result;
}

void
ValidatorList::updatePublisherList(
    PublicKey const& pubKey,
    PublisherList const& current,
    std::vector<PublicKey> const& oldList,
    ValidatorList::lock_guard const&)
{
    // Update keyListings_ for added and removed keys
    std::vector<PublicKey> const& publisherList = current.list;
    std::vector<std::string> const& manifests = current.manifests;
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
}

ValidatorList::PublisherListStats
ValidatorList::applyList(
    std::string const& globalManifest,
    std::optional<std::string> const& localManifest,
    std::string const& blob,
    std::string const& signature,
    std::uint32_t version,
    std::string siteUri,
    std::optional<uint256> const& hash,
    ValidatorList::lock_guard const& lock)
{
    using namespace std::string_literals;

    Json::Value list;
    PublicKey pubKey;
    auto const& manifest = localManifest ? *localManifest : globalManifest;
    auto const result = verify(lock, list, pubKey, manifest, blob, signature);
    if (result > ListDisposition::pending)
    {
        if (publisherLists_.count(pubKey))
        {
            auto const& pubCollection = publisherLists_[pubKey];
            if (pubCollection.maxSequence &&
                (result == ListDisposition::same_sequence ||
                 result == ListDisposition::known_sequence))
            {
                // We've seen something valid list for this publisher
                // already, so return what we know about it.
                return PublisherListStats{
                    result,
                    pubKey,
                    pubCollection.status,
                    *pubCollection.maxSequence};
            }
        }
        return PublisherListStats{result};
    }

    // Update publisher's list
    auto& pubCollection = publisherLists_[pubKey];
    auto const sequence = list[jss::sequence].asUInt();
    auto const accepted =
        (result == ListDisposition::accepted ||
         result == ListDisposition::expired);

    if (accepted)
        pubCollection.status = result == ListDisposition::accepted
            ? PublisherStatus::available
            : PublisherStatus::expired;
    pubCollection.rawManifest = globalManifest;
    if (!pubCollection.maxSequence || sequence > *pubCollection.maxSequence)
        pubCollection.maxSequence = sequence;

    Json::Value const& newList = list[jss::validators];
    std::vector<PublicKey> oldList;
    if (accepted && pubCollection.remaining.count(sequence) != 0)
    {
        // We've seen this list before and stored it in "remaining". The
        // normal expected process is that the processed list would have
        // already been moved in to "current" by "updateTrusted()", but race
        // conditions are possible, or the node may have lost sync, so do
        // some of that work here.
        auto& publisher = pubCollection.current;
        // Copy the old validator list
        oldList = std::move(pubCollection.current.list);
        // Move the publisher info from "remaining" to "current"
        publisher = std::move(pubCollection.remaining[sequence]);
        // Remove the entry in "remaining"
        pubCollection.remaining.erase(sequence);
        // Done
        assert(publisher.sequence == sequence);
    }
    else
    {
        auto& publisher = accepted ? pubCollection.current
                                   : pubCollection.remaining[sequence];
        publisher.sequence = sequence;
        publisher.validFrom = TimeKeeper::time_point{TimeKeeper::duration{
            list.isMember(jss::effective) ? list[jss::effective].asUInt() : 0}};
        publisher.validUntil = TimeKeeper::time_point{
            TimeKeeper::duration{list[jss::expiration].asUInt()}};
        publisher.siteUri = std::move(siteUri);
        publisher.rawBlob = blob;
        publisher.rawSignature = signature;
        publisher.rawManifest = localManifest;
        if (hash)
            publisher.hash = *hash;

        std::vector<PublicKey>& publisherList = publisher.list;
        std::vector<std::string>& manifests = publisher.manifests;

        // Copy the old validator list
        oldList = std::move(publisherList);
        // Build the new validator list from "newList"
        publisherList.clear();
        publisherList.reserve(newList.size());
        for (auto const& val : newList)
        {
            if (val.isObject() && val.isMember(jss::validation_public_key) &&
                val[jss::validation_public_key].isString())
            {
                std::optional<Blob> const ret =
                    strUnHex(val[jss::validation_public_key].asString());

                if (!ret || !publicKeyType(makeSlice(*ret)))
                {
                    JLOG(j_.error())
                        << "Invalid node identity: "
                        << val[jss::validation_public_key].asString();
                }
                else
                {
                    publisherList.push_back(
                        PublicKey(Slice{ret->data(), ret->size()}));
                }

                if (val.isMember(jss::manifest) &&
                    val[jss::manifest].isString())
                    manifests.push_back(val[jss::manifest].asString());
            }
        }

        // Standardize the list order by sorting
        std::sort(publisherList.begin(), publisherList.end());
    }
    // If this publisher has ever sent a more updated version than the one
    // in this file, keep it. This scenario is unlikely, but legal.
    pubCollection.rawVersion = std::max(pubCollection.rawVersion, version);
    if (!pubCollection.remaining.empty())
    {
        // If there are any pending VLs, then this collection must be at least
        // version 2.
        pubCollection.rawVersion = std::max(pubCollection.rawVersion, 2u);
    }

    PublisherListStats const applyResult{
        result, pubKey, pubCollection.status, *pubCollection.maxSequence};

    if (accepted)
    {
        updatePublisherList(pubKey, pubCollection.current, oldList, lock);
    }

    return applyResult;
}

std::vector<std::string>
ValidatorList::loadLists()
{
    using namespace std::string_literals;
    using namespace boost::filesystem;
    using namespace boost::system::errc;

    std::lock_guard lock{mutex_};

    std::vector<std::string> sites;
    sites.reserve(publisherLists_.size());
    for (auto const& [pubKey, publisherCollection] : publisherLists_)
    {
        boost::system::error_code ec;

        if (publisherCollection.status == PublisherStatus::available)
            continue;

        boost::filesystem::path const filename = getCacheFileName(lock, pubKey);

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
    ValidatorList::lock_guard const& lock,
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
        removePublisherList(lock, pubKey, PublisherStatus::revoked);
        // If the manifest is revoked, no future list is valid either
        publisherLists_[pubKey].remaining.clear();
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

    if (list.isMember(jss::sequence) && list[jss::sequence].isInt() &&
        list.isMember(jss::expiration) && list[jss::expiration].isInt() &&
        (!list.isMember(jss::effective) || list[jss::effective].isInt()) &&
        list.isMember(jss::validators) && list[jss::validators].isArray())
    {
        auto const sequence = list[jss::sequence].asUInt();
        auto const validFrom = TimeKeeper::time_point{TimeKeeper::duration{
            list.isMember(jss::effective) ? list[jss::effective].asUInt() : 0}};
        auto const validUntil = TimeKeeper::time_point{
            TimeKeeper::duration{list[jss::expiration].asUInt()}};
        auto const now = timeKeeper_.now();
        auto const& listCollection = publisherLists_[pubKey];
        if (validUntil <= validFrom)
            return ListDisposition::invalid;
        else if (sequence < listCollection.current.sequence)
            return ListDisposition::stale;
        else if (sequence == listCollection.current.sequence)
            return ListDisposition::same_sequence;
        else if (validUntil <= now)
            return ListDisposition::expired;
        else if (validFrom > now)
            // Not yet valid. Return pending if one of the following is true
            // * There's no maxSequence, indicating this is the first blob seen
            //   for this publisher
            // * The sequence is larger than the maxSequence, indicating this
            //   blob is new
            // * There's no entry for this sequence AND this blob is valid
            //   before the last blob, indicating blobs may be processing out of
            //    order. This may result in some duplicated processing, but
            //   prevents the risk of missing valid data. Else return
            //   known_sequence
            return !listCollection.maxSequence ||
                    sequence > *listCollection.maxSequence ||
                    (listCollection.remaining.count(sequence) == 0 &&
                     validFrom < listCollection.remaining
                                     .at(*listCollection.maxSequence)
                                     .validFrom)
                ? ListDisposition::pending
                : ListDisposition::known_sequence;
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
    std::shared_lock read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey(identity);
    return keyListings_.find(pubKey) != keyListings_.end();
}

bool
ValidatorList::trusted(
    ValidatorList::shared_lock const&,
    PublicKey const& identity) const
{
    auto const pubKey = validatorManifests_.getMasterKey(identity);
    return trustedMasterKeys_.find(pubKey) != trustedMasterKeys_.end();
}

bool
ValidatorList::trusted(PublicKey const& identity) const
{
    std::shared_lock read_lock{mutex_};
    return trusted(read_lock, identity);
}

std::optional<PublicKey>
ValidatorList::getListedKey(PublicKey const& identity) const
{
    std::shared_lock read_lock{mutex_};

    auto const pubKey = validatorManifests_.getMasterKey(identity);
    if (keyListings_.find(pubKey) != keyListings_.end())
        return pubKey;
    return std::nullopt;
}

std::optional<PublicKey>
ValidatorList::getTrustedKey(
    ValidatorList::shared_lock const&,
    PublicKey const& identity) const
{
    auto const pubKey = validatorManifests_.getMasterKey(identity);
    if (trustedMasterKeys_.find(pubKey) != trustedMasterKeys_.end())
        return pubKey;
    return std::nullopt;
}

std::optional<PublicKey>
ValidatorList::getTrustedKey(PublicKey const& identity) const
{
    std::shared_lock read_lock{mutex_};

    return getTrustedKey(read_lock, identity);
}

bool
ValidatorList::trustedPublisher(PublicKey const& identity) const
{
    std::shared_lock read_lock{mutex_};
    return identity.size() && publisherLists_.count(identity) &&
        publisherLists_.at(identity).status < PublisherStatus::revoked;
}

PublicKey
ValidatorList::localPublicKey() const
{
    std::shared_lock read_lock{mutex_};
    return localPubKey_;
}

bool
ValidatorList::removePublisherList(
    ValidatorList::lock_guard const&,
    PublicKey const& publisherKey,
    PublisherStatus reason)
{
    assert(
        reason != PublisherStatus::available &&
        reason != PublisherStatus::unavailable);
    auto const iList = publisherLists_.find(publisherKey);
    if (iList == publisherLists_.end())
        return false;

    JLOG(j_.debug()) << "Removing validator list for publisher "
                     << strHex(publisherKey);

    for (auto const& val : iList->second.current.list)
    {
        auto const& iVal = keyListings_.find(val);
        if (iVal == keyListings_.end())
            continue;

        if (iVal->second <= 1)
            keyListings_.erase(iVal);
        else
            --iVal->second;
    }

    iList->second.current.list.clear();
    iList->second.status = reason;

    return true;
}

std::size_t
ValidatorList::count(ValidatorList::shared_lock const&) const
{
    return publisherLists_.size();
}

std::size_t
ValidatorList::count() const
{
    std::shared_lock read_lock{mutex_};
    return count(read_lock);
}

std::optional<TimeKeeper::time_point>
ValidatorList::expires(ValidatorList::shared_lock const&) const
{
    std::optional<TimeKeeper::time_point> res{};
    for (auto const& [pubKey, collection] : publisherLists_)
    {
        (void)pubKey;
        // Unfetched
        auto const& current = collection.current;
        if (current.validUntil == TimeKeeper::time_point{})
            return std::nullopt;

        // Find the latest validUntil in a chain where the next validFrom
        // overlaps with the previous validUntil. applyLists has already cleaned
        // up the list so the validFrom dates are guaranteed increasing.
        auto chainedExpiration = current.validUntil;
        for (auto const& [sequence, check] : collection.remaining)
        {
            (void)sequence;
            if (check.validFrom <= chainedExpiration)
                chainedExpiration = check.validUntil;
            else
                break;
        }

        // Earliest
        if (!res || chainedExpiration < *res)
        {
            res = chainedExpiration;
        }
    }
    return res;
}

std::optional<TimeKeeper::time_point>
ValidatorList::expires() const
{
    std::shared_lock read_lock{mutex_};
    return expires(read_lock);
}

Json::Value
ValidatorList::getJson() const
{
    Json::Value res(Json::objectValue);

    std::shared_lock read_lock{mutex_};

    res[jss::validation_quorum] = static_cast<Json::UInt>(quorum_);

    {
        auto& x = (res[jss::validator_list] = Json::objectValue);

        x[jss::count] = static_cast<Json::UInt>(count(read_lock));

        if (auto when = expires(read_lock))
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
        for (auto const& key : it->second.current.list)
            jLocalStaticKeys.append(toBase58(TokenType::NodePublic, key));
    }

    // Publisher lists
    Json::Value& jPublisherLists =
        (res[jss::publisher_lists] = Json::arrayValue);
    for (auto const& [publicKey, pubCollection] : publisherLists_)
    {
        if (local == publicKey)
            continue;
        Json::Value& curr = jPublisherLists.append(Json::objectValue);
        curr[jss::pubkey_publisher] = strHex(publicKey);
        curr[jss::available] =
            pubCollection.status == PublisherStatus::available;

        auto appendList = [](PublisherList const& publisherList,
                             Json::Value& target) {
            target[jss::uri] = publisherList.siteUri;
            if (publisherList.validUntil != TimeKeeper::time_point{})
            {
                target[jss::seq] =
                    static_cast<Json::UInt>(publisherList.sequence);
                target[jss::expiration] = to_string(publisherList.validUntil);
            }
            if (publisherList.validFrom != TimeKeeper::time_point{})
                target[jss::effective] = to_string(publisherList.validFrom);
            Json::Value& keys = (target[jss::list] = Json::arrayValue);
            for (auto const& key : publisherList.list)
            {
                keys.append(toBase58(TokenType::NodePublic, key));
            }
        };
        {
            auto const& current = pubCollection.current;
            appendList(current, curr);
            if (current.validUntil != TimeKeeper::time_point{})
            {
                curr[jss::version] = pubCollection.rawVersion;
            }
        }

        Json::Value remaining(Json::arrayValue);
        for (auto const& [sequence, future] : pubCollection.remaining)
        {
            using namespace std::chrono_literals;

            (void)sequence;
            Json::Value& r = remaining.append(Json::objectValue);
            appendList(future, r);
            // Race conditions can happen, so make this check "fuzzy"
            assert(future.validFrom > timeKeeper_.now() + 600s);
        }
        if (remaining.size())
            curr[jss::remaining] = std::move(remaining);
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

    // Negative UNL
    if (!negativeUNL_.empty())
    {
        Json::Value& jNegativeUNL = (res[jss::NegativeUNL] = Json::arrayValue);
        for (auto const& k : negativeUNL_)
        {
            jNegativeUNL.append(toBase58(TokenType::NodePublic, k));
        }
    }

    return res;
}

void
ValidatorList::for_each_listed(
    std::function<void(PublicKey const&, bool)> func) const
{
    std::shared_lock read_lock{mutex_};

    for (auto const& v : keyListings_)
        func(v.first, trusted(read_lock, v.first));
}

void
ValidatorList::for_each_available(
    std::function<void(
        std::string const& manifest,
        std::uint32_t version,
        std::map<std::size_t, ValidatorBlobInfo> const& blobInfos,
        PublicKey const& pubKey,
        std::size_t maxSequence,
        uint256 const& hash)> func) const
{
    std::shared_lock read_lock{mutex_};

    for (auto const& [key, plCollection] : publisherLists_)
    {
        if (plCollection.status != PublisherStatus::available || key.empty())
            continue;
        assert(plCollection.maxSequence);
        func(
            plCollection.rawManifest,
            plCollection.rawVersion,
            buildBlobInfos(plCollection),
            key,
            plCollection.maxSequence.value_or(0),
            plCollection.fullHash);
    }
}

std::optional<Json::Value>
ValidatorList::getAvailable(
    boost::beast::string_view const& pubKey,
    std::optional<std::uint32_t> forceVersion /* = {} */)
{
    std::shared_lock read_lock{mutex_};

    auto const keyBlob = strViewUnHex(pubKey);

    if (!keyBlob || !publicKeyType(makeSlice(*keyBlob)))
    {
        JLOG(j_.info()) << "Invalid requested validator list publisher key: "
                        << pubKey;
        return {};
    }

    auto id = PublicKey(makeSlice(*keyBlob));

    auto const iter = publisherLists_.find(id);

    if (iter == publisherLists_.end() ||
        iter->second.status != PublisherStatus::available)
        return {};

    Json::Value value =
        buildFileData(std::string{pubKey}, iter->second, forceVersion, j_);

    return value;
}

std::size_t
ValidatorList::calculateQuorum(
    std::size_t unlSize,
    std::size_t effectiveUnlSize,
    std::size_t seenSize)
{
    // Use quorum if specified via command line.
    if (minimumQuorum_ > 0)
    {
        JLOG(j_.warn()) << "Using potentially unsafe quorum of "
                        << *minimumQuorum_
                        << " as specified on the command line";
        return *minimumQuorum_;
    }

    // Do not use achievable quorum until lists from all configured
    // publishers are available
    for (auto const& list : publisherLists_)
    {
        if (list.second.status != PublisherStatus::available)
            return std::numeric_limits<std::size_t>::max();
    }

    // Use an 80% quorum to balance fork safety, liveness, and required UNL
    // overlap.
    //
    // Theorem 8 of the Analysis of the XRP Ledger Consensus Protocol
    // (https://arxiv.org/abs/1802.07242) says:
    //     XRP LCP guarantees fork safety if Oi,j > nj/2 + ni − qi + ti,j
    //     for every pair of nodes Pi, Pj.
    //
    // ni: size of Pi's UNL
    // nj: size of Pj's UNL
    // Oi,j: number of validators in both UNLs
    // qi: validation quorum for Pi's UNL
    // ti, tj: maximum number of allowed Byzantine faults in Pi and Pj's
    // UNLs ti,j: min{ti, tj, Oi,j}
    //
    // Assume ni < nj, meaning and ti,j = ti
    //
    // For qi = .8*ni, we make ti <= .2*ni
    // (We could make ti lower and tolerate less UNL overlap. However in
    // order to prioritize safety over liveness, we need ti >= ni - qi)
    //
    // An 80% quorum allows two UNLs to safely have < .2*ni unique
    // validators between them:
    //
    // pi = ni - Oi,j
    // pj = nj - Oi,j
    //
    // Oi,j > nj/2 + ni − qi + ti,j
    // ni - pi > (ni - pi + pj)/2 + ni − .8*ni + .2*ni
    // pi + pj < .2*ni
    //
    // Note that the negative UNL protocol introduced the
    // AbsoluteMinimumQuorum which is 60% of the original UNL size. The
    // effective quorum should not be lower than it.
    return static_cast<std::size_t>(std::max(
        std::ceil(effectiveUnlSize * 0.8f), std::ceil(unlSize * 0.6f)));
}

TrustChanges
ValidatorList::updateTrusted(
    hash_set<NodeID> const& seenValidators,
    NetClock::time_point closeTime,
    NetworkOPs& ops,
    Overlay& overlay,
    HashRouter& hashRouter)
{
    using namespace std::chrono_literals;
    if (timeKeeper_.now() > closeTime + 30s)
        closeTime = timeKeeper_.now();

    std::lock_guard lock{mutex_};

    // Rotate pending and remove expired published lists
    bool good = true;
    for (auto& [pubKey, collection] : publisherLists_)
    {
        {
            auto& remaining = collection.remaining;
            auto const firstIter = remaining.begin();
            auto iter = firstIter;
            if (iter != remaining.end() && iter->second.validFrom <= closeTime)
            {
                // Find the LAST candidate that is ready to go live.
                for (auto next = std::next(iter); next != remaining.end() &&
                     next->second.validFrom <= closeTime;
                     ++iter, ++next)
                {
                    assert(std::next(iter) == next);
                }
                assert(iter != remaining.end());

                // Rotate the pending list in to current
                auto sequence = iter->first;
                auto& candidate = iter->second;
                auto& current = collection.current;
                assert(candidate.validFrom <= closeTime);

                auto const oldList = current.list;
                current = std::move(candidate);
                if (collection.status != PublisherStatus::available)
                    collection.status = PublisherStatus::available;
                assert(current.sequence == sequence);
                // If the list is expired, remove the validators so they don't
                // get processed in. The expiration check below will do the rest
                // of the work
                if (current.validUntil <= closeTime)
                    current.list.clear();

                updatePublisherList(pubKey, current, oldList, lock);

                // Only broadcast the current, which will consequently only
                // send to peers that don't understand v2, or which are
                // unknown (unlikely). Those that do understand v2 should
                // already have this list and are in the process of
                // switching themselves.
                broadcastBlobs(
                    pubKey,
                    collection,
                    sequence,
                    current.hash,
                    overlay,
                    hashRouter,
                    j_);

                // Erase any candidates that we skipped over, plus this one
                remaining.erase(firstIter, std::next(iter));
            }
        }
        // Remove if expired
        if (collection.status == PublisherStatus::available &&
            collection.current.validUntil <= closeTime)
        {
            removePublisherList(lock, pubKey, PublisherStatus::expired);
            ops.setUNLBlocked();
        }
        if (collection.status != PublisherStatus::available)
            good = false;
    }
    if (good)
        ops.clearUNLBlocked();

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

    // If there were any changes, we need to update the ephemeral signing
    // keys:
    if (!trustChanges.added.empty() || !trustChanges.removed.empty())
    {
        trustedSigningKeys_.clear();

        for (auto const& k : trustedMasterKeys_)
            trustedSigningKeys_.insert(validatorManifests_.getSigningKey(k));
    }

    JLOG(j_.debug())
        << trustedMasterKeys_.size() << "  of " << keyListings_.size()
        << " listed validators eligible for inclusion in the trusted set";

    auto const unlSize = trustedMasterKeys_.size();
    auto effectiveUnlSize = unlSize;
    auto seenSize = seenValidators.size();
    if (!negativeUNL_.empty())
    {
        for (auto const& k : trustedMasterKeys_)
        {
            if (negativeUNL_.count(k))
                --effectiveUnlSize;
        }
        hash_set<NodeID> negUnlNodeIDs;
        for (auto const& k : negativeUNL_)
        {
            negUnlNodeIDs.emplace(calcNodeID(k));
        }
        for (auto const& nid : seenValidators)
        {
            if (negUnlNodeIDs.count(nid))
                --seenSize;
        }
    }
    quorum_ = calculateQuorum(unlSize, effectiveUnlSize, seenSize);

    JLOG(j_.debug()) << "Using quorum of " << quorum_ << " for new set of "
                     << unlSize << " trusted validators ("
                     << trustChanges.added.size() << " added, "
                     << trustChanges.removed.size() << " removed)";

    if (unlSize < quorum_)
    {
        JLOG(j_.warn()) << "New quorum of " << quorum_
                        << " exceeds the number of trusted validators ("
                        << unlSize << ")";
    }

    if (publisherLists_.size() && unlSize == 0)
    {
        // No validators. Lock down.
        ops.setUNLBlocked();
    }

    return trustChanges;
}

hash_set<PublicKey>
ValidatorList::getTrustedMasterKeys() const
{
    std::shared_lock read_lock{mutex_};
    return trustedMasterKeys_;
}

hash_set<PublicKey>
ValidatorList::getNegativeUNL() const
{
    std::shared_lock read_lock{mutex_};
    return negativeUNL_;
}

void
ValidatorList::setNegativeUNL(hash_set<PublicKey> const& negUnl)
{
    std::lock_guard lock{mutex_};
    negativeUNL_ = negUnl;
}

std::vector<std::shared_ptr<STValidation>>
ValidatorList::negativeUNLFilter(
    std::vector<std::shared_ptr<STValidation>>&& validations) const
{
    // Remove validations that are from validators on the negative UNL.
    auto ret = std::move(validations);

    std::shared_lock read_lock{mutex_};
    if (!negativeUNL_.empty())
    {
        ret.erase(
            std::remove_if(
                ret.begin(),
                ret.end(),
                [&](auto const& v) -> bool {
                    if (auto const masterKey =
                            getTrustedKey(read_lock, v->getSignerPublic());
                        masterKey)
                    {
                        return negativeUNL_.count(*masterKey);
                    }
                    else
                    {
                        return false;
                    }
                }),
            ret.end());
    }

    return ret;
}

}  // namespace ripple
