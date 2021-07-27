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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/app/rdb/RelationalDBInterface.h>
#include <ripple/app/rdb/RelationalDBInterface_global.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/P2PConfigImpl.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/overlay/predicates.h>
#include <ripple/peerfinder/make_Manager.h>
#include <ripple/rpc/handlers/GetCounts.h>
#include <ripple/rpc/json_body.h>
#include <ripple/server/SimpleWriter.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/utility/in_place_factory.hpp>

namespace ripple {

namespace CrawlOptions {
enum {
    Disabled = 0,
    Overlay = (1 << 0),
    ServerInfo = (1 << 1),
    ServerCounts = (1 << 2),
    Unl = (1 << 3)
};
}

//------------------------------------------------------------------------------

OverlayImpl::OverlayImpl(
    Application& app,
    Setup const& setup,
    std::uint16_t overlayPort,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : P2POverlayImpl(
          std::make_unique<P2PConfigImpl>(app),
          setup,
          overlayPort,
          resourceManager,
          resolver,
          io_service,
          config,
          collector)
    , app_(app)
    , timer_count_(0)
    , slots_(app, *this)
    , m_stats(
          std::bind(&OverlayImpl::collect_metrics, this),
          collector,
          [counts = m_traffic.getCounts(), collector]() {
              std::vector<TrafficGauges> ret;
              ret.reserve(counts.size());

              for (size_t i = 0; i < counts.size(); ++i)
              {
                  ret.push_back(TrafficGauges(counts[i].name, collector));
              }

              return ret;
          }())
{
    beast::PropertyStream::Source::add(m_peerFinder.get());
}

//------------------------------------------------------------------------------

void
OverlayImpl::onEvtTimer()
{
    if ((++timer_count_ % Tuning::checkIdlePeers) == 0)
        deleteIdlePeers();
}

void
OverlayImpl::start()
{
    P2POverlayImpl::start();
}

void
OverlayImpl::stop()
{
    P2POverlayImpl::stop();
}

//------------------------------------------------------------------------------
//
// PropertyStream
//
//------------------------------------------------------------------------------

void
OverlayImpl::onWrite(beast::PropertyStream::Map& stream)
{
    beast::PropertyStream::Set set("traffic", stream);
    auto const stats = m_traffic.getCounts();
    for (auto const& i : stats)
    {
        if (i)
        {
            beast::PropertyStream::Map item(set);
            item["category"] = i.name;
            item["bytes_in"] = std::to_string(i.bytesIn.load());
            item["messages_in"] = std::to_string(i.messagesIn.load());
            item["bytes_out"] = std::to_string(i.bytesOut.load());
            item["messages_out"] = std::to_string(i.messagesOut.load());
        }
    }
}

void
OverlayImpl::onManifests(
    std::shared_ptr<protocol::TMManifests> const& m,
    std::shared_ptr<PeerImp> const& from)
{
    auto const n = m->list_size();
    auto const& journal = from->pjournal();

    protocol::TMManifests relay;

    for (std::size_t i = 0; i < n; ++i)
    {
        auto& s = m->list().Get(i).stobject();

        if (auto mo = deserializeManifest(s))
        {
            auto const serialized = mo->serialized;

            auto const result =
                app_.validatorManifests().applyManifest(std::move(*mo));

            if (result == ManifestDisposition::accepted)
            {
                relay.add_list()->set_stobject(s);

                // N.B.: this is important; the applyManifest call above moves
                //       the loaded Manifest out of the optional so we need to
                //       reload it here.
                mo = deserializeManifest(serialized);
                assert(mo);

                app_.getOPs().pubManifest(*mo);

                if (app_.validators().listed(mo->masterKey))
                {
                    auto db = app_.getWalletDB().checkoutDb();
                    addValidatorManifest(*db, serialized);
                }
            }
        }
        else
        {
            JLOG(journal.debug())
                << "Malformed manifest #" << i + 1 << ": " << strHex(s);
            continue;
        }
    }

    if (!relay.list().empty())
        for_each([m2 = std::make_shared<Message>(relay, protocol::mtMANIFESTS)](
                     std::shared_ptr<PeerImp>&& p) { p->send(m2); });
}

void
OverlayImpl::reportTraffic(
    TrafficCount::category cat,
    bool isInbound,
    int number)
{
    m_traffic.addCount(cat, isInbound, number);
}

Json::Value
OverlayImpl::crawlShards(bool includePublicKey, std::uint32_t relays)
{
    using namespace std::chrono;

    Json::Value jv(Json::objectValue);

    // Add shard info from this server to json result
    if (auto shardStore = app_.getShardStore())
    {
        if (includePublicKey)
            jv[jss::public_key] =
                toBase58(TokenType::NodePublic, app_.nodeIdentity().first);

        auto const shardInfo{shardStore->getShardInfo()};
        if (!shardInfo->finalized().empty())
            jv[jss::complete_shards] = shardInfo->finalizedToString();
        if (!shardInfo->incomplete().empty())
            jv[jss::incomplete_shards] = shardInfo->incompleteToString();
    }

    if (relays == 0 || size() == 0)
        return jv;

    {
        protocol::TMGetPeerShardInfoV2 tmGPS;
        tmGPS.set_relays(relays);

        // Wait if a request is in progress
        std::unique_lock<std::mutex> csLock{csMutex_};
        if (!csIDs_.empty())
            csCV_.wait(csLock);

        foreach([&](auto const& peer) { csIDs_.emplace(peer->id()); });

        // Request peer shard info
        foreach(send_always(std::make_shared<Message>(
            tmGPS, protocol::mtGET_PEER_SHARD_INFO_V2)));

        if (csCV_.wait_for(csLock, seconds(60)) == std::cv_status::timeout)
        {
            csIDs_.clear();
            csCV_.notify_all();
        }
    }

    // Combine shard info from peers
    hash_map<PublicKey, NodeStore::ShardInfo> peerShardInfo;
    for_each([&](std::shared_ptr<PeerImp>&& peer) {
        auto const psi{peer->getPeerShardInfos()};
        for (auto const& [publicKey, shardInfo] : psi)
        {
            auto const it{peerShardInfo.find(publicKey)};
            if (it == peerShardInfo.end())
                peerShardInfo.emplace(publicKey, shardInfo);
            else if (shardInfo.msgTimestamp() > it->second.msgTimestamp())
                it->second = shardInfo;
        }
    });

    // Add shard info to json result
    if (!peerShardInfo.empty())
    {
        auto& av = jv[jss::peers] = Json::Value(Json::arrayValue);
        for (auto const& [publicKey, shardInfo] : peerShardInfo)
        {
            auto& pv{av.append(Json::Value(Json::objectValue))};
            if (includePublicKey)
            {
                pv[jss::public_key] =
                    toBase58(TokenType::NodePublic, publicKey);
            }

            if (!shardInfo.finalized().empty())
                pv[jss::complete_shards] = shardInfo.finalizedToString();
            if (!shardInfo.incomplete().empty())
                pv[jss::incomplete_shards] = shardInfo.incompleteToString();
        }
    }

    return jv;
}

void
OverlayImpl::endOfPeerChain(std::uint32_t id)
{
    // Notify threads if all peers have received a reply from all peer chains
    std::lock_guard csLock{csMutex_};
    csIDs_.erase(id);
    if (csIDs_.empty())
        csCV_.notify_all();
}

Json::Value
OverlayImpl::getOverlayInfo()
{
    using namespace std::chrono;
    Json::Value jv;
    auto& av = jv["active"] = Json::Value(Json::arrayValue);

    for_each([&](std::shared_ptr<PeerImp>&& sp) {
        auto& pv = av.append(Json::Value(Json::objectValue));
        pv[jss::public_key] = base64_encode(
            sp->getNodePublic().data(), sp->getNodePublic().size());
        pv[jss::type] = sp->slot()->inbound() ? "in" : "out";
        pv[jss::uptime] = static_cast<std::uint32_t>(
            duration_cast<seconds>(sp->uptime()).count());
        if (sp->crawl())
        {
            pv[jss::ip] = sp->getRemoteAddress().address().to_string();
            if (sp->slot()->inbound())
            {
                if (auto port = sp->slot()->listening_port())
                    pv[jss::port] = *port;
            }
            else
            {
                pv[jss::port] = std::to_string(sp->getRemoteAddress().port());
            }
        }

        {
            auto version{sp->getVersion()};
            if (!version.empty())
                // Could move here if Json::value supported moving from strings
                pv[jss::version] = version;
        }

        std::uint32_t minSeq, maxSeq;
        sp->ledgerRange(minSeq, maxSeq);
        if (minSeq != 0 || maxSeq != 0)
            pv[jss::complete_ledgers] =
                std::to_string(minSeq) + "-" + std::to_string(maxSeq);

        auto const peerShardInfos{sp->getPeerShardInfos()};
        auto const it{peerShardInfos.find(sp->getNodePublic())};
        if (it != peerShardInfos.end())
        {
            auto const& shardInfo{it->second};
            if (!shardInfo.finalized().empty())
                pv[jss::complete_shards] = shardInfo.finalizedToString();
            if (!shardInfo.incomplete().empty())
                pv[jss::incomplete_shards] = shardInfo.incompleteToString();
        }
    });

    return jv;
}

Json::Value
OverlayImpl::getServerInfo()
{
    bool const humanReadable = false;
    bool const admin = false;
    bool const counters = false;

    Json::Value server_info =
        app_.getOPs().getServerInfo(humanReadable, admin, counters);

    // Filter out some information
    server_info.removeMember(jss::hostid);
    server_info.removeMember(jss::load_factor_fee_escalation);
    server_info.removeMember(jss::load_factor_fee_queue);
    server_info.removeMember(jss::validation_quorum);

    if (server_info.isMember(jss::validated_ledger))
    {
        Json::Value& validated_ledger = server_info[jss::validated_ledger];

        validated_ledger.removeMember(jss::base_fee);
        validated_ledger.removeMember(jss::reserve_base_xrp);
        validated_ledger.removeMember(jss::reserve_inc_xrp);
    }

    return server_info;
}

Json::Value
OverlayImpl::getServerCounts()
{
    return getCountsJson(app_, 10);
}

Json::Value
OverlayImpl::getUnlInfo()
{
    Json::Value validators = app_.validators().getJson();

    if (validators.isMember(jss::publisher_lists))
    {
        Json::Value& publisher_lists = validators[jss::publisher_lists];

        for (auto& publisher : publisher_lists)
        {
            publisher.removeMember(jss::list);
        }
    }

    validators.removeMember(jss::signing_keys);
    validators.removeMember(jss::trusted_validator_keys);
    validators.removeMember(jss::validation_quorum);

    Json::Value validatorSites = app_.validatorSites().getJson();

    if (validatorSites.isMember(jss::validator_sites))
    {
        validators[jss::validator_sites] =
            std::move(validatorSites[jss::validator_sites]);
    }

    return validators;
}

// Returns information on verified peers.
Json::Value
OverlayImpl::json()
{
    Json::Value json;
    for (auto const& peer : getActivePeers())
    {
        json.append(peer->json());
    }
    return json;
}

bool
OverlayImpl::processCrawl(http_request_type const& req, Handoff& handoff)
{
    if (req.target() != "/crawl" ||
        setup_.crawlOptions == CrawlOptions::Disabled)
        return false;

    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.result(boost::beast::http::status::ok);
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");
    msg.body()["version"] = Json::Value(2u);

    if (setup_.crawlOptions & CrawlOptions::Overlay)
    {
        msg.body()["overlay"] = getOverlayInfo();
    }
    if (setup_.crawlOptions & CrawlOptions::ServerInfo)
    {
        msg.body()["server"] = getServerInfo();
    }
    if (setup_.crawlOptions & CrawlOptions::ServerCounts)
    {
        msg.body()["counts"] = getServerCounts();
    }
    if (setup_.crawlOptions & CrawlOptions::Unl)
    {
        msg.body()["unl"] = getUnlInfo();
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

bool
OverlayImpl::processValidatorList(
    http_request_type const& req,
    Handoff& handoff)
{
    // If the target is in the form "/vl/<validator_list_public_key>",
    // return the most recent validator list for that key.
    constexpr std::string_view prefix("/vl/");

    if (!req.target().starts_with(prefix.data()) || !setup_.vlEnabled)
        return false;

    std::uint32_t version = 1;

    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto fail = [&msg, &handoff](auto status) {
        msg.result(status);
        msg.insert("Content-Length", "0");

        msg.body() = Json::nullValue;

        msg.prepare_payload();
        handoff.response = std::make_shared<SimpleWriter>(msg);
        return true;
    };

    auto key = req.target().substr(prefix.size());

    if (auto slash = key.find('/'); slash != boost::string_view::npos)
    {
        auto verString = key.substr(0, slash);
        if (!boost::conversion::try_lexical_convert(verString, version))
            return fail(boost::beast::http::status::bad_request);
        key = key.substr(slash + 1);
    }

    if (key.empty())
        return fail(boost::beast::http::status::bad_request);

    // find the list
    auto vl = app_.validators().getAvailable(key, version);

    if (!vl)
    {
        // 404 not found
        return fail(boost::beast::http::status::not_found);
    }
    else if (!*vl)
    {
        return fail(boost::beast::http::status::bad_request);
    }
    else
    {
        msg.result(boost::beast::http::status::ok);

        msg.body() = *vl;

        msg.prepare_payload();
        handoff.response = std::make_shared<SimpleWriter>(msg);
        return true;
    }
}

bool
OverlayImpl::processHealth(http_request_type const& req, Handoff& handoff)
{
    if (req.target() != "/health")
        return false;
    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto info = getServerInfo();

    int last_validated_ledger_age = -1;
    if (info.isMember("validated_ledger"))
        last_validated_ledger_age = info["validated_ledger"]["age"].asInt();
    bool amendment_blocked = false;
    if (info.isMember("amendment_blocked"))
        amendment_blocked = true;
    int number_peers = info["peers"].asInt();
    std::string server_state = info["server_state"].asString();
    auto load_factor =
        info["load_factor"].asDouble() / info["load_base"].asDouble();

    enum { healthy, warning, critical };
    int health = healthy;
    auto set_health = [&health](int state) {
        if (health < state)
            health = state;
    };

    msg.body()[jss::info] = Json::objectValue;
    if (last_validated_ledger_age >= 7 || last_validated_ledger_age < 0)
    {
        msg.body()[jss::info]["validated_ledger"] = last_validated_ledger_age;
        if (last_validated_ledger_age < 20)
            set_health(warning);
        else
            set_health(critical);
    }

    if (amendment_blocked)
    {
        msg.body()[jss::info]["amendment_blocked"] = true;
        set_health(critical);
    }

    if (number_peers <= 7)
    {
        msg.body()[jss::info]["peers"] = number_peers;
        if (number_peers != 0)
            set_health(warning);
        else
            set_health(critical);
    }

    if (!(server_state == "full" || server_state == "validating" ||
          server_state == "proposing"))
    {
        msg.body()[jss::info]["server_state"] = server_state;
        if (server_state == "syncing" || server_state == "tracking" ||
            server_state == "connected")
        {
            set_health(warning);
        }
        else
            set_health(critical);
    }

    if (load_factor > 100)
    {
        msg.body()[jss::info]["load_factor"] = load_factor;
        if (load_factor < 1000)
            set_health(warning);
        else
            set_health(critical);
    }

    switch (health)
    {
        case healthy:
            msg.result(boost::beast::http::status::ok);
            break;
        case warning:
            msg.result(boost::beast::http::status::service_unavailable);
            break;
        case critical:
            msg.result(boost::beast::http::status::internal_server_error);
            break;
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

bool
OverlayImpl::onEvtProcessRequest(http_request_type const& req, Handoff& handoff)
{
    // Take advantage of || short-circuiting
    return processCrawl(req, handoff) || processValidatorList(req, handoff) ||
        processHealth(req, handoff);
}

void
OverlayImpl::checkTracking(std::uint32_t index)
{
    for_each(
        [index](std::shared_ptr<PeerImp>&& sp) { sp->checkTracking(index); });
}

void
OverlayImpl::broadcast(protocol::TMProposeSet& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER);
    for_each([&](std::shared_ptr<PeerImp>&& p) { p->send(sm); });
}

std::set<Peer::id_t>
OverlayImpl::relay(
    protocol::TMProposeSet& m,
    uint256 const& uid,
    PublicKey const& validator)
{
    if (auto const toSkip = app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm =
            std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER, validator);
        for_each([&](std::shared_ptr<PeerImp>&& p) {
            if (toSkip->find(p->id()) == toSkip->end())
                p->send(sm);
        });
        return *toSkip;
    }
    return {};
}

void
OverlayImpl::broadcast(protocol::TMValidation& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtVALIDATION);
    for_each([sm](std::shared_ptr<PeerImp>&& p) { p->send(sm); });
}

std::set<Peer::id_t>
OverlayImpl::relay(
    protocol::TMValidation& m,
    uint256 const& uid,
    PublicKey const& validator)
{
    if (auto const toSkip = app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm =
            std::make_shared<Message>(m, protocol::mtVALIDATION, validator);
        for_each([&](std::shared_ptr<PeerImp>&& p) {
            if (toSkip->find(p->id()) == toSkip->end())
                p->send(sm);
        });
        return *toSkip;
    }
    return {};
}

std::shared_ptr<Message>
OverlayImpl::getManifestsMessage()
{
    std::lock_guard g(manifestLock_);

    if (auto seq = app_.validatorManifests().sequence();
        seq != manifestListSeq_)
    {
        protocol::TMManifests tm;

        app_.validatorManifests().for_each_manifest(
            [&tm](std::size_t s) { tm.mutable_list()->Reserve(s); },
            [&tm, &hr = app_.getHashRouter()](Manifest const& manifest) {
                tm.add_list()->set_stobject(
                    manifest.serialized.data(), manifest.serialized.size());
                hr.addSuppression(manifest.hash());
            });

        manifestMessage_.reset();

        if (tm.list_size() != 0)
            manifestMessage_ =
                std::make_shared<Message>(tm, protocol::mtMANIFESTS);

        manifestListSeq_ = seq;
    }

    return manifestMessage_;
}

//------------------------------------------------------------------------------

std::shared_ptr<Message>
makeSquelchMessage(
    PublicKey const& validator,
    bool squelch,
    uint32_t squelchDuration)
{
    protocol::TMSquelch m;
    m.set_squelch(squelch);
    m.set_validatorpubkey(validator.data(), validator.size());
    if (squelch)
        m.set_squelchduration(squelchDuration);
    return std::make_shared<Message>(m, protocol::mtSQUELCH);
}

void
OverlayImpl::unsquelch(PublicKey const& validator, Peer::id_t id) const
{
    if (auto peer = findPeerByShortID(id);
        peer && app_.config().VP_REDUCE_RELAY_SQUELCH)
    {
        // optimize - multiple message with different
        // validator might be sent to the same peer
        peer->send(makeSquelchMessage(validator, false, 0));
    }
}

void
OverlayImpl::squelch(
    PublicKey const& validator,
    Peer::id_t id,
    uint32_t squelchDuration) const
{
    if (auto peer = findPeerByShortID(id);
        peer && app_.config().VP_REDUCE_RELAY_SQUELCH)
    {
        peer->send(makeSquelchMessage(validator, true, squelchDuration));
    }
}

void
OverlayImpl::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    std::set<Peer::id_t>&& peers,
    protocol::MessageType type)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            [this, key, validator, peers = std::move(peers), type]() mutable {
                updateSlotAndSquelch(key, validator, std::move(peers), type);
            });

    for (auto id : peers)
        slots_.updateSlotAndSquelch(key, validator, id, type);
}

void
OverlayImpl::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    Peer::id_t peer,
    protocol::MessageType type)
{
    if (!strand_.running_in_this_thread())
        return post(strand_, [this, key, validator, peer, type]() {
            updateSlotAndSquelch(key, validator, peer, type);
        });

    slots_.updateSlotAndSquelch(key, validator, peer, type);
}

void
OverlayImpl::deletePeer(Peer::id_t id)
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&OverlayImpl::deletePeer, this, id));

    slots_.deletePeer(id, true);
}

void
OverlayImpl::deleteIdlePeers()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&OverlayImpl::deleteIdlePeers, this));

    slots_.deleteIdlePeers();
}

std::shared_ptr<PeerImp>
OverlayImpl::mkInboundPeer(
    Peer::id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& stream_ptr)
{
    return std::make_shared<PeerImp>(
        app_,
        id,
        slot,
        std::move(request),
        publicKey,
        protocol,
        consumer,
        std::move(stream_ptr),
        *this);
}

std::shared_ptr<PeerImp>
OverlayImpl::mkOutboundPeer(
    std::unique_ptr<stream_type>&& stream_ptr,
    boost::beast::multi_buffer const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Peer::id_t id)
{
    return std::make_shared<PeerImp>(
        app_,
        std::move(stream_ptr),
        buffers.data(),
        std::move(slot),
        std::move(response),
        usage,
        publicKey,
        protocol,
        id,
        *this);
}

//------------------------------------------------------------------------------

Overlay::Setup
setup_Overlay(BasicConfig const& config)
{
    Overlay::Setup setup;

    {
        auto const& section = config.section("overlay");
        setup.context = make_SSLContext("");

        set(setup.ipLimit, "ip_limit", section);
        if (setup.ipLimit < 0)
            Throw<std::runtime_error>("Configured IP limit is invalid");

        std::string ip;
        set(ip, "public_ip", section);
        if (!ip.empty())
        {
            boost::system::error_code ec;
            setup.public_ip = beast::IP::Address::from_string(ip, ec);
            if (ec || beast::IP::is_private(setup.public_ip))
                Throw<std::runtime_error>("Configured public IP is invalid");
        }
    }

    {
        auto const& section = config.section("crawl");
        auto const& values = section.values();

        if (values.size() > 1)
        {
            Throw<std::runtime_error>(
                "Configured [crawl] section is invalid, too many values");
        }

        bool crawlEnabled = true;

        // Only allow "0|1" as a value
        if (values.size() == 1)
        {
            try
            {
                crawlEnabled = boost::lexical_cast<bool>(values.front());
            }
            catch (boost::bad_lexical_cast const&)
            {
                Throw<std::runtime_error>(
                    "Configured [crawl] section has invalid value: " +
                    values.front());
            }
        }

        if (crawlEnabled)
        {
            if (get<bool>(section, "overlay", true))
            {
                setup.crawlOptions |= CrawlOptions::Overlay;
            }
            if (get<bool>(section, "server", true))
            {
                setup.crawlOptions |= CrawlOptions::ServerInfo;
            }
            if (get<bool>(section, "counts", false))
            {
                setup.crawlOptions |= CrawlOptions::ServerCounts;
            }
            if (get<bool>(section, "unl", true))
            {
                setup.crawlOptions |= CrawlOptions::Unl;
            }
        }
    }
    {
        auto const& section = config.section("vl");

        set(setup.vlEnabled, "enabled", section);
    }

    try
    {
        auto id = config.legacy("network_id");

        if (!id.empty())
        {
            if (id == "main")
                id = "0";

            if (id == "testnet")
                id = "1";

            if (id == "devnet")
                id = "2";

            setup.networkID = beast::lexicalCastThrow<std::uint32_t>(id);
        }
    }
    catch (...)
    {
        Throw<std::runtime_error>(
            "Configured [network_id] section is invalid: must be a number "
            "or one of the strings 'main', 'testnet' or 'devnet'.");
    }

    return setup;
}

std::unique_ptr<Overlay>
make_Overlay(
    Application& app,
    Overlay::Setup const& setup,
    std::uint16_t overlayPort,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<OverlayImpl>(
        app,
        setup,
        overlayPort,
        resourceManager,
        resolver,
        io_service,
        config,
        collector);
}

}  // namespace ripple
