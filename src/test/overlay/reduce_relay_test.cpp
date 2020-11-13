//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2020 Ripple Labs Inc.

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
#include <ripple/basics/random.h>
#include <ripple/beast/unit_test.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/Slot.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple.pb.h>
#include <test/jtx/Env.h>

#include <boost/thread.hpp>

#include <numeric>
#include <optional>

namespace ripple {

namespace test {

using namespace std::chrono;

class Link;

using MessageSPtr = std::shared_ptr<Message>;
using LinkSPtr = std::shared_ptr<Link>;
using PeerSPtr = std::shared_ptr<Peer>;
using PeerWPtr = std::weak_ptr<Peer>;
using SquelchCB =
    std::function<void(PublicKey const&, PeerWPtr const&, std::uint32_t)>;
using UnsquelchCB = std::function<void(PublicKey const&, PeerWPtr const&)>;
using LinkIterCB = std::function<void(Link&, MessageSPtr)>;

static constexpr std::uint32_t MAX_PEERS = 10;
static constexpr std::uint32_t MAX_VALIDATORS = 10;
static constexpr std::uint32_t MAX_MESSAGES = 200000;

/** Simulate two entities - peer directly connected to the server
 * (via squelch in PeerSim) and PeerImp (via Overlay)
 */
class PeerPartial : public Peer
{
public:
    virtual ~PeerPartial()
    {
    }
    virtual void
    onMessage(MessageSPtr const& m, SquelchCB f) = 0;
    virtual void
    onMessage(protocol::TMSquelch const& squelch) = 0;
    void
    send(protocol::TMSquelch const& squelch)
    {
        onMessage(squelch);
    }

    // dummy implementation
    void
    send(std::shared_ptr<Message> const& m) override
    {
    }
    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return {};
    }
    void
    charge(Resource::Charge const& fee) override
    {
    }
    bool
    cluster() const override
    {
        return false;
    }
    bool
    isHighLatency() const override
    {
        return false;
    }
    int
    getScore(bool) const override
    {
        return 0;
    }
    PublicKey const&
    getNodePublic() const override
    {
        static PublicKey key{};
        return key;
    }
    Json::Value
    json() override
    {
        return {};
    }
    bool
    supportsFeature(ProtocolFeature f) const override
    {
        return false;
    }
    std::optional<std::size_t>
    publisherListSequence(PublicKey const&) const override
    {
        return {};
    }
    void
    setPublisherListSequence(PublicKey const&, std::size_t const) override
    {
    }
    uint256 const&
    getClosedLedgerHash() const override
    {
        static uint256 hash{};
        return hash;
    }
    bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const override
    {
        return false;
    }
    void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const override
    {
    }
    bool
    hasShard(std::uint32_t shardIndex) const override
    {
        return false;
    }
    bool
    hasTxSet(uint256 const& hash) const override
    {
        return false;
    }
    void
    cycleStatus() override
    {
    }
    bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) override
    {
        return false;
    }
    bool
    compressionEnabled() const override
    {
        return false;
    }
};

/** Manually advanced clock. */
class ManualClock
{
public:
    typedef uint64_t rep;
    typedef std::milli period;
    typedef std::chrono::duration<std::uint32_t, period> duration;
    typedef std::chrono::time_point<ManualClock> time_point;
    inline static const bool is_steady = false;

    static void
    advance(duration d) noexcept
    {
        now_ += d;
    }

    static void
    randAdvance(milliseconds min, milliseconds max)
    {
        now_ += randDuration(min, max);
    }

    static void
    reset() noexcept
    {
        now_ = time_point(seconds(0));
    }

    static time_point
    now() noexcept
    {
        return now_;
    }

    static duration
    randDuration(milliseconds min, milliseconds max)
    {
        return duration(milliseconds(rand_int(min.count(), max.count())));
    }

    explicit ManualClock() = default;

private:
    inline static time_point now_ = time_point(seconds(0));
};

/** Simulate server's OverlayImpl */
class Overlay
{
public:
    Overlay() = default;
    virtual ~Overlay() = default;

    virtual void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t id,
        SquelchCB f,
        protocol::MessageType type = protocol::mtVALIDATION) = 0;

    virtual void deleteIdlePeers(UnsquelchCB) = 0;

    virtual void deletePeer(Peer::id_t, UnsquelchCB) = 0;
};

class Validator;

/** Simulate link from a validator to a peer directly connected
 * to the server.
 */
class Link
{
    using Latency = std::pair<milliseconds, milliseconds>;

public:
    Link(
        Validator& validator,
        PeerSPtr peer,
        Latency const& latency = {milliseconds(5), milliseconds(15)})
        : validator_(validator), peer_(peer), latency_(latency), up_(true)
    {
        auto sp = peer_.lock();
        assert(sp);
    }
    ~Link() = default;
    void
    send(MessageSPtr const& m, SquelchCB f)
    {
        if (!up_)
            return;
        auto sp = peer_.lock();
        assert(sp);
        auto peer = std::dynamic_pointer_cast<PeerPartial>(sp);
        peer->onMessage(m, f);
    }
    Validator&
    validator()
    {
        return validator_;
    }
    void
    up(bool linkUp)
    {
        up_ = linkUp;
    }
    Peer::id_t
    peerId()
    {
        auto p = peer_.lock();
        assert(p);
        return p->id();
    }
    PeerSPtr
    getPeer()
    {
        auto p = peer_.lock();
        assert(p);
        return p;
    }

private:
    Validator& validator_;
    PeerWPtr peer_;
    Latency latency_;
    bool up_;
};

/** Simulate Validator */
class Validator
{
    using Links = std::unordered_map<Peer::id_t, LinkSPtr>;

public:
    Validator()
    {
        pkey_ = std::get<0>(randomKeyPair(KeyType::ed25519));
        protocol::TMValidation v;
        v.set_validation("validation");
        message_ = std::make_shared<Message>(v, protocol::mtVALIDATION, pkey_);
        id_ = sid_++;
    }
    Validator(Validator const&) = default;
    Validator(Validator&&) = default;
    Validator&
    operator=(Validator const&) = default;
    Validator&
    operator=(Validator&&) = default;
    ~Validator()
    {
        clear();
    }

    void
    clear()
    {
        links_.clear();
    }

    static void
    resetId()
    {
        sid_ = 0;
    }

    PublicKey const&
    key()
    {
        return pkey_;
    }

    operator PublicKey() const
    {
        return pkey_;
    }

    void
    addPeer(PeerSPtr peer)
    {
        links_.emplace(
            std::make_pair(peer->id(), std::make_shared<Link>(*this, peer)));
    }

    void
    deletePeer(Peer::id_t id)
    {
        links_.erase(id);
    }

    void
    for_links(std::vector<Peer::id_t> peers, LinkIterCB f)
    {
        for (auto id : peers)
        {
            assert(links_.find(id) != links_.end());
            f(*links_[id], message_);
        }
    }

    void
    for_links(LinkIterCB f, bool simulateSlow = false)
    {
        std::vector<LinkSPtr> v;
        std::transform(
            links_.begin(), links_.end(), std::back_inserter(v), [](auto& kv) {
                return kv.second;
            });
        std::random_device d;
        std::mt19937 g(d());
        std::shuffle(v.begin(), v.end(), g);

        for (auto& link : v)
        {
            f(*link, message_);
        }
    }

    /** Send to specific peers */
    void
    send(std::vector<Peer::id_t> peers, SquelchCB f)
    {
        for_links(peers, [&](Link& link, MessageSPtr m) { link.send(m, f); });
    }

    /** Send to all peers */
    void
    send(SquelchCB f)
    {
        for_links([&](Link& link, MessageSPtr m) { link.send(m, f); });
    }

    MessageSPtr
    message()
    {
        return message_;
    }

    std::uint16_t
    id()
    {
        return id_;
    }

    void
    linkUp(Peer::id_t id)
    {
        auto it = links_.find(id);
        assert(it != links_.end());
        it->second->up(true);
    }

    void
    linkDown(Peer::id_t id)
    {
        auto it = links_.find(id);
        assert(it != links_.end());
        it->second->up(false);
    }

private:
    Links links_;
    PublicKey pkey_{};
    MessageSPtr message_ = nullptr;
    inline static std::uint16_t sid_ = 0;
    std::uint16_t id_ = 0;
};

class PeerSim : public PeerPartial, public std::enable_shared_from_this<PeerSim>
{
public:
    using id_t = Peer::id_t;
    PeerSim(Overlay& overlay, beast::Journal journal)
        : overlay_(overlay), squelch_(journal)
    {
        id_ = sid_++;
    }

    ~PeerSim() = default;

    id_t
    id() const override
    {
        return id_;
    }

    static void
    resetId()
    {
        sid_ = 0;
    }

    /** Local Peer (PeerImp) */
    void
    onMessage(MessageSPtr const& m, SquelchCB f) override
    {
        auto validator = m->getValidatorKey();
        assert(validator);
        if (!squelch_.expireSquelch(*validator))
            return;

        overlay_.updateSlotAndSquelch({}, *validator, id(), f);
    }

    /** Remote Peer (Directly connected Peer) */
    virtual void
    onMessage(protocol::TMSquelch const& squelch) override
    {
        auto validator = squelch.validatorpubkey();
        PublicKey key(Slice(validator.data(), validator.size()));
        if (squelch.squelch())
            squelch_.addSquelch(
                key, std::chrono::seconds{squelch.squelchduration()});
        else
            squelch_.removeSquelch(key);
    }

private:
    inline static id_t sid_ = 0;
    id_t id_;
    Overlay& overlay_;
    reduce_relay::Squelch<ManualClock> squelch_;
};

class OverlaySim : public Overlay, public reduce_relay::SquelchHandler
{
    using Peers = std::unordered_map<Peer::id_t, PeerSPtr>;

public:
    using id_t = Peer::id_t;
    using clock_type = ManualClock;
    OverlaySim(Application& app) : slots_(app, *this), app_(app)
    {
    }

    ~OverlaySim() = default;

    void
    clear()
    {
        peers_.clear();
        ManualClock::advance(hours(1));
        slots_.deleteIdlePeers();
    }

    std::uint16_t
    inState(PublicKey const& validator, reduce_relay::PeerState state)
    {
        auto res = slots_.inState(validator, state);
        return res ? *res : 0;
    }

    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t id,
        SquelchCB f,
        protocol::MessageType type = protocol::mtVALIDATION) override
    {
        squelch_ = f;
        slots_.updateSlotAndSquelch(key, validator, id, type);
    }

    void
    deletePeer(id_t id, UnsquelchCB f) override
    {
        unsquelch_ = f;
        slots_.deletePeer(id, true);
    }

    void
    deleteIdlePeers(UnsquelchCB f) override
    {
        unsquelch_ = f;
        slots_.deleteIdlePeers();
    }

    PeerSPtr
    addPeer(bool useCache = true)
    {
        PeerSPtr peer{};
        Peer::id_t id;
        if (peersCache_.empty() || !useCache)
        {
            peer = std::make_shared<PeerSim>(*this, app_.journal("Squelch"));
            id = peer->id();
        }
        else
        {
            auto it = peersCache_.begin();
            peer = it->second;
            id = it->first;
            peersCache_.erase(it);
        }
        peers_.emplace(std::make_pair(id, peer));
        return peer;
    }

    void
    deletePeer(Peer::id_t id, bool useCache = true)
    {
        auto it = peers_.find(id);
        assert(it != peers_.end());
        deletePeer(id, [&](PublicKey const&, PeerWPtr) {});
        if (useCache)
            peersCache_.emplace(std::make_pair(id, it->second));
        peers_.erase(it);
    }

    void
    resetPeers()
    {
        while (!peers_.empty())
            deletePeer(peers_.begin()->first);
        while (!peersCache_.empty())
            addPeer();
    }

    std::optional<Peer::id_t>
    deleteLastPeer()
    {
        if (peers_.empty())
            return {};

        std::uint8_t maxId = 0;

        for (auto& [id, _] : peers_)
        {
            (void)_;
            if (id > maxId)
                maxId = id;
        }

        deletePeer(maxId, false);

        return maxId;
    }

    bool
    isCountingState(PublicKey const& validator)
    {
        return slots_.inState(validator, reduce_relay::SlotState::Counting);
    }

    std::set<id_t>
    getSelected(PublicKey const& validator)
    {
        return slots_.getSelected(validator);
    }

    bool
    isSelected(PublicKey const& validator, Peer::id_t peer)
    {
        auto selected = slots_.getSelected(validator);
        return selected.find(peer) != selected.end();
    }

    id_t
    getSelectedPeer(PublicKey const& validator)
    {
        auto selected = slots_.getSelected(validator);
        assert(selected.size());
        return *selected.begin();
    }

    std::unordered_map<
        id_t,
        std::tuple<
            reduce_relay::PeerState,
            std::uint16_t,
            std::uint32_t,
            std::uint32_t>>
    getPeers(PublicKey const& validator)
    {
        return slots_.getPeers(validator);
    }

    std::uint16_t
    getNumPeers() const
    {
        return peers_.size();
    }

private:
    void
    squelch(
        PublicKey const& validator,
        Peer::id_t id,
        std::uint32_t squelchDuration) const override
    {
        if (auto it = peers_.find(id); it != peers_.end())
            squelch_(validator, it->second, squelchDuration);
    }
    void
    unsquelch(PublicKey const& validator, Peer::id_t id) const override
    {
        if (auto it = peers_.find(id); it != peers_.end())
            unsquelch_(validator, it->second);
    }
    SquelchCB squelch_;
    UnsquelchCB unsquelch_;
    Peers peers_;
    Peers peersCache_;
    reduce_relay::Slots<ManualClock> slots_;
    Application& app_;
};

class Network
{
public:
    Network(Application& app) : overlay_(app)
    {
        init();
    }

    void
    init()
    {
        validators_.resize(MAX_VALIDATORS);
        for (int p = 0; p < MAX_PEERS; p++)
        {
            auto peer = overlay_.addPeer();
            for (auto& v : validators_)
                v.addPeer(peer);
        }
    }

    ~Network() = default;

    void
    reset()
    {
        validators_.clear();
        overlay_.clear();
        PeerSim::resetId();
        Validator::resetId();
        init();
    }

    Peer::id_t
    addPeer()
    {
        auto peer = overlay_.addPeer();
        for (auto& v : validators_)
            v.addPeer(peer);
        return peer->id();
    }

    void
    deleteLastPeer()
    {
        auto id = overlay_.deleteLastPeer();

        if (!id)
            return;

        for (auto& validator : validators_)
            validator.deletePeer(*id);
    }

    void
    purgePeers()
    {
        while (overlay_.getNumPeers() > MAX_PEERS)
            deleteLastPeer();
    }

    Validator&
    validator(std::uint16_t v)
    {
        assert(v < validators_.size());
        return validators_[v];
    }

    OverlaySim&
    overlay()
    {
        return overlay_;
    }

    void
    enableLink(std::uint16_t validatorId, Peer::id_t peer, bool enable)
    {
        auto it =
            std::find_if(validators_.begin(), validators_.end(), [&](auto& v) {
                return v.id() == validatorId;
            });
        assert(it != validators_.end());
        if (enable)
            it->linkUp(peer);
        else
            it->linkDown(peer);
    }

    void
    onDisconnectPeer(Peer::id_t peer)
    {
        // Send unsquelch to the Peer on all links. This way when
        // the Peer "reconnects" it starts sending messages on the link.
        // We expect that if a Peer disconnects and then reconnects, it's
        // unsquelched.
        protocol::TMSquelch squelch;
        squelch.set_squelch(false);
        for (auto& v : validators_)
        {
            PublicKey key = v;
            squelch.clear_validatorpubkey();
            squelch.set_validatorpubkey(key.data(), key.size());
            v.for_links({peer}, [&](Link& l, MessageSPtr) {
                std::dynamic_pointer_cast<PeerSim>(l.getPeer())->send(squelch);
            });
        }
    }

    void
    for_rand(
        std::uint32_t min,
        std::uint32_t max,
        std::function<void(std::uint32_t)> f)
    {
        auto size = max - min;
        std::vector<std::uint32_t> s(size);
        std::iota(s.begin(), s.end(), min);
        std::random_device d;
        std::mt19937 g(d());
        std::shuffle(s.begin(), s.end(), g);
        for (auto v : s)
            f(v);
    }

    void
    propagate(
        LinkIterCB link,
        std::uint16_t nValidators = MAX_VALIDATORS,
        std::uint32_t nMessages = MAX_MESSAGES,
        bool purge = true,
        bool resetClock = true)
    {
        if (resetClock)
            ManualClock::reset();

        if (purge)
        {
            purgePeers();
            overlay_.resetPeers();
        }

        for (int m = 0; m < nMessages; ++m)
        {
            ManualClock::randAdvance(milliseconds(1800), milliseconds(2200));
            for_rand(0, nValidators, [&](std::uint32_t v) {
                validators_[v].for_links(link);
            });
        }
    }

    /** Is peer in Selected state in any of the slots */
    bool
    isSelected(Peer::id_t id)
    {
        for (auto& v : validators_)
        {
            if (overlay_.isSelected(v, id))
                return true;
        }
        return false;
    }

    /** Check if there are peers to unsquelch - peer is in Selected
     * state in any of the slots and there are peers in Squelched state
     * in those slots.
     */
    bool
    allCounting(Peer::id_t peer)
    {
        for (auto& v : validators_)
        {
            if (!overlay_.isSelected(v, peer))
                continue;
            auto peers = overlay_.getPeers(v);
            for (auto& [_, v] : peers)
            {
                (void)_;
                if (std::get<reduce_relay::PeerState>(v) ==
                    reduce_relay::PeerState::Squelched)
                    return false;
            }
        }
        return true;
    }

private:
    OverlaySim overlay_;
    std::vector<Validator> validators_;
};

class reduce_relay_test : public beast::unit_test::suite
{
    using Slot = reduce_relay::Slot<ManualClock>;
    using id_t = Peer::id_t;

protected:
    void
    printPeers(const std::string& msg, std::uint16_t validator = 0)
    {
        auto peers = network_.overlay().getPeers(network_.validator(validator));
        std::cout << msg << " "
                  << "num peers " << (int)network_.overlay().getNumPeers()
                  << std::endl;
        for (auto& [k, v] : peers)
            std::cout << k << ":" << (int)std::get<reduce_relay::PeerState>(v)
                      << " ";
        std::cout << std::endl;
    }

    /** Send squelch (if duration is set) or unsquelch (if duration not set) */
    Peer::id_t
    sendSquelch(
        PublicKey const& validator,
        PeerWPtr const& peerPtr,
        std::optional<std::uint32_t> duration)
    {
        protocol::TMSquelch squelch;
        bool res = duration ? true : false;
        squelch.set_squelch(res);
        squelch.set_validatorpubkey(validator.data(), validator.size());
        if (res)
            squelch.set_squelchduration(*duration);
        auto sp = peerPtr.lock();
        assert(sp);
        std::dynamic_pointer_cast<PeerSim>(sp)->send(squelch);
        return sp->id();
    }

    enum State { On, Off, WaitReset };
    enum EventType { LinkDown = 0, PeerDisconnected = 1 };
    // Link down or Peer disconnect event
    // TBD - add new peer event
    // TBD - add overlapping type of events at any
    //       time in any quantity
    struct Event
    {
        State state_ = State::Off;
        std::uint32_t cnt_ = 0;
        std::uint32_t handledCnt_ = 0;
        bool isSelected_ = false;
        Peer::id_t peer_;
        std::uint16_t validator_;
        PublicKey key_;
        time_point<ManualClock> time_;
        bool handled_ = false;
    };

    /** Randomly brings the link between a validator and a peer down.
     * Randomly disconnects a peer. Those events are generated one at a time.
     */
    void
    random(bool log)
    {
        std::unordered_map<EventType, Event> events{
            {LinkDown, {}}, {PeerDisconnected, {}}};
        time_point<ManualClock> lastCheck = ManualClock::now();

        network_.reset();
        network_.propagate([&](Link& link, MessageSPtr m) {
            auto& validator = link.validator();
            auto now = ManualClock::now();

            bool squelched = false;
            std::stringstream str;

            link.send(
                m,
                [&](PublicKey const& key,
                    PeerWPtr const& peerPtr,
                    std::uint32_t duration) {
                    assert(key == validator);
                    auto p = sendSquelch(key, peerPtr, duration);
                    squelched = true;
                    str << p << " ";
                });

            if (squelched)
            {
                auto selected = network_.overlay().getSelected(validator);
                str << " selected: ";
                for (auto s : selected)
                    str << s << " ";
                if (log)
                    std::cout
                        << (double)reduce_relay::epoch<milliseconds>(now)
                                .count() /
                            1000.
                        << " random, squelched, validator: " << validator.id()
                        << " peers: " << str.str() << std::endl;
                auto countingState =
                    network_.overlay().isCountingState(validator);
                BEAST_EXPECT(
                    countingState == false &&
                    selected.size() == reduce_relay::MAX_SELECTED_PEERS);
            }

            // Trigger Link Down or Peer Disconnect event
            // Only one Link Down at a time
            if (events[EventType::LinkDown].state_ == State::Off)
            {
                auto update = [&](EventType event) {
                    events[event].cnt_++;
                    events[event].validator_ = validator.id();
                    events[event].key_ = validator;
                    events[event].peer_ = link.peerId();
                    events[event].state_ = State::On;
                    events[event].time_ = now;
                    if (event == EventType::LinkDown)
                    {
                        network_.enableLink(
                            validator.id(), link.peerId(), false);
                        events[event].isSelected_ =
                            network_.overlay().isSelected(
                                validator, link.peerId());
                    }
                    else
                        events[event].isSelected_ =
                            network_.isSelected(link.peerId());
                };
                auto r = rand_int(0, 1000);
                if (r == (int)EventType::LinkDown ||
                    r == (int)EventType::PeerDisconnected)
                {
                    update(static_cast<EventType>(r));
                }
            }

            if (events[EventType::PeerDisconnected].state_ == State::On)
            {
                auto& event = events[EventType::PeerDisconnected];
                bool allCounting = network_.allCounting(event.peer_);
                network_.overlay().deletePeer(
                    event.peer_,
                    [&](PublicKey const& v, PeerWPtr const& peerPtr) {
                        if (event.isSelected_)
                            sendSquelch(v, peerPtr, {});
                        event.handled_ = true;
                    });
                // Should only be unsquelched if the peer is in Selected state
                // If in Selected state it's possible unsquelching didn't
                // take place because there is no peers in Squelched state in
                // any of the slots where the peer is in Selected state
                // (allCounting is true)
                bool handled =
                    (event.isSelected_ == false && !event.handled_) ||
                    (event.isSelected_ == true &&
                     (event.handled_ || allCounting));
                BEAST_EXPECT(handled);
                event.state_ = State::Off;
                event.isSelected_ = false;
                event.handledCnt_ += handled;
                event.handled_ = false;
                network_.onDisconnectPeer(event.peer_);
            }

            auto& event = events[EventType::LinkDown];
            // Check every sec for idled peers. Idled peers are
            // created by Link Down event.
            if (now - lastCheck > milliseconds(1000))
            {
                lastCheck = now;
                // Check if Link Down event must be handled by
                // deleteIdlePeer(): 1) the peer is in Selected state;
                // 2) the peer has not received any messages for IDLED time;
                // 3) there are peers in Squelched state in the slot.
                // 4) peer is in Slot's peers_ (if not then it is deleted
                //    by Slots::deleteIdlePeers())
                bool mustHandle = false;
                if (event.state_ == State::On)
                {
                    event.isSelected_ =
                        network_.overlay().isSelected(event.key_, event.peer_);
                    auto peers = network_.overlay().getPeers(event.key_);
                    auto d = reduce_relay::epoch<milliseconds>(now).count() -
                        std::get<3>(peers[event.peer_]);
                    mustHandle = event.isSelected_ &&
                        d > milliseconds(reduce_relay::IDLED).count() &&
                        network_.overlay().inState(
                            event.key_, reduce_relay::PeerState::Squelched) >
                            0 &&
                        peers.find(event.peer_) != peers.end();
                }
                network_.overlay().deleteIdlePeers(
                    [&](PublicKey const& v, PeerWPtr const& ptr) {
                        event.handled_ = true;
                        if (mustHandle && v == event.key_)
                        {
                            event.state_ = State::WaitReset;
                            sendSquelch(validator, ptr, {});
                        }
                    });
                bool handled =
                    (event.handled_ && event.state_ == State::WaitReset) ||
                    (!event.handled_ && !mustHandle);
                BEAST_EXPECT(handled);
            }
            if (event.state_ == State::WaitReset ||
                (event.state_ == State::On &&
                 (now - event.time_ > (reduce_relay::IDLED + seconds(2)))))
            {
                bool handled =
                    event.state_ == State::WaitReset || !event.handled_;
                BEAST_EXPECT(handled);
                event.state_ = State::Off;
                event.isSelected_ = false;
                event.handledCnt_ += handled;
                event.handled_ = false;
                network_.enableLink(event.validator_, event.peer_, true);
            }
        });

        auto& down = events[EventType::LinkDown];
        auto& disconnected = events[EventType::PeerDisconnected];
        // It's possible the last Down Link event is not handled
        BEAST_EXPECT(down.handledCnt_ >= down.cnt_ - 1);
        // All Peer Disconnect events must be handled
        BEAST_EXPECT(disconnected.cnt_ == disconnected.handledCnt_);
        if (log)
            std::cout << "link down count: " << down.cnt_ << "/"
                      << down.handledCnt_
                      << " peer disconnect count: " << disconnected.cnt_ << "/"
                      << disconnected.handledCnt_;
    }

    bool
    checkCounting(PublicKey const& validator, bool isCountingState)
    {
        auto countingState = network_.overlay().isCountingState(validator);
        BEAST_EXPECT(countingState == isCountingState);
        return countingState == isCountingState;
    }

    void
    doTest(const std::string& msg, bool log, std::function<void(bool)> f)
    {
        testcase(msg);
        f(log);
    }

    /** Initial counting round: three peers receive message "faster" then
     * others. Once the message count for the three peers reaches threshold
     * the rest of the peers are squelched and the slot for the given validator
     * is in Selected state.
     */
    void
    testInitialRound(bool log)
    {
        doTest("Initial Round", log, [this](bool log) {
            BEAST_EXPECT(propagateAndSquelch(log));
        });
    }

    /** Receiving message from squelched peer too soon should not change the
     * slot's state to Counting.
     */
    void
    testPeerUnsquelchedTooSoon(bool log)
    {
        doTest("Peer Unsquelched Too Soon", log, [this](bool log) {
            BEAST_EXPECT(propagateNoSquelch(log, 1, false, false, false));
        });
    }

    /** Receiving message from squelched peer should change the
     * slot's state to Counting.
     */
    void
    testPeerUnsquelched(bool log)
    {
        ManualClock::advance(seconds(601));
        doTest("Peer Unsquelched", log, [this](bool log) {
            BEAST_EXPECT(propagateNoSquelch(log, 2, true, true, false));
        });
    }

    /** Propagate enough messages to generate one squelch event */
    bool
    propagateAndSquelch(bool log, bool purge = true, bool resetClock = true)
    {
        int n = 0;
        network_.propagate(
            [&](Link& link, MessageSPtr message) {
                std::uint16_t squelched = 0;
                link.send(
                    message,
                    [&](PublicKey const& key,
                        PeerWPtr const& peerPtr,
                        std::uint32_t duration) {
                        squelched++;
                        sendSquelch(key, peerPtr, duration);
                    });
                if (squelched)
                {
                    BEAST_EXPECT(
                        squelched ==
                        MAX_PEERS - reduce_relay::MAX_SELECTED_PEERS);
                    n++;
                }
            },
            1,
            reduce_relay::MAX_MESSAGE_THRESHOLD + 2,
            purge,
            resetClock);
        auto selected = network_.overlay().getSelected(network_.validator(0));
        BEAST_EXPECT(selected.size() == reduce_relay::MAX_SELECTED_PEERS);
        BEAST_EXPECT(n == 1);  // only one selection round
        auto res = checkCounting(network_.validator(0), false);
        BEAST_EXPECT(res);
        return n == 1 && res;
    }

    /** Send fewer message so that squelch event is not generated */
    bool
    propagateNoSquelch(
        bool log,
        std::uint16_t nMessages,
        bool countingState,
        bool purge = true,
        bool resetClock = true)
    {
        bool squelched = false;
        network_.propagate(
            [&](Link& link, MessageSPtr message) {
                link.send(
                    message,
                    [&](PublicKey const& key,
                        PeerWPtr const& peerPtr,
                        std::uint32_t duration) {
                        squelched = true;
                        BEAST_EXPECT(false);
                    });
            },
            1,
            nMessages,
            purge,
            resetClock);
        auto res = checkCounting(network_.validator(0), countingState);
        return !squelched && res;
    }

    /** Receiving a message from new peer should change the
     * slot's state to Counting.
     */
    void
    testNewPeer(bool log)
    {
        doTest("New Peer", log, [this](bool log) {
            BEAST_EXPECT(propagateAndSquelch(log, true, false));
            network_.addPeer();
            BEAST_EXPECT(propagateNoSquelch(log, 1, true, false, false));
        });
    }

    /** Selected peer disconnects. Should change the state to counting and
     * unsquelch squelched peers. */
    void
    testSelectedPeerDisconnects(bool log)
    {
        doTest("Selected Peer Disconnects", log, [this](bool log) {
            ManualClock::advance(seconds(601));
            BEAST_EXPECT(propagateAndSquelch(log, true, false));
            auto id = network_.overlay().getSelectedPeer(network_.validator(0));
            std::uint16_t unsquelched = 0;
            network_.overlay().deletePeer(
                id, [&](PublicKey const& key, PeerWPtr const& peer) {
                    unsquelched++;
                });
            BEAST_EXPECT(
                unsquelched == MAX_PEERS - reduce_relay::MAX_SELECTED_PEERS);
            BEAST_EXPECT(checkCounting(network_.validator(0), true));
        });
    }

    /** Selected peer stops relaying. Should change the state to counting and
     * unsquelch squelched peers. */
    void
    testSelectedPeerStopsRelaying(bool log)
    {
        doTest("Selected Peer Stops Relaying", log, [this](bool log) {
            ManualClock::advance(seconds(601));
            BEAST_EXPECT(propagateAndSquelch(log, true, false));
            ManualClock::advance(reduce_relay::IDLED + seconds(1));
            std::uint16_t unsquelched = 0;
            network_.overlay().deleteIdlePeers(
                [&](PublicKey const& key, PeerWPtr const& peer) {
                    unsquelched++;
                });
            auto peers = network_.overlay().getPeers(network_.validator(0));
            BEAST_EXPECT(
                unsquelched == MAX_PEERS - reduce_relay::MAX_SELECTED_PEERS);
            BEAST_EXPECT(checkCounting(network_.validator(0), true));
        });
    }

    /** Squelched peer disconnects. Should not change the state to counting.
     */
    void
    testSquelchedPeerDisconnects(bool log)
    {
        doTest("Squelched Peer Disconnects", log, [this](bool log) {
            ManualClock::advance(seconds(601));
            BEAST_EXPECT(propagateAndSquelch(log, true, false));
            auto peers = network_.overlay().getPeers(network_.validator(0));
            auto it = std::find_if(peers.begin(), peers.end(), [&](auto it) {
                return std::get<reduce_relay::PeerState>(it.second) ==
                    reduce_relay::PeerState::Squelched;
            });
            assert(it != peers.end());
            std::uint16_t unsquelched = 0;
            network_.overlay().deletePeer(
                it->first, [&](PublicKey const& key, PeerWPtr const& peer) {
                    unsquelched++;
                });
            BEAST_EXPECT(unsquelched == 0);
            BEAST_EXPECT(checkCounting(network_.validator(0), false));
        });
    }

    void
    testConfig(bool log)
    {
        doTest("Config Test", log, [&](bool log) {
            Config c;

            std::string toLoad(R"rippleConfig(
[reduce_relay]
vp_enable=1
vp_squelch=1
)rippleConfig");

            c.loadFromString(toLoad);
            BEAST_EXPECT(c.VP_REDUCE_RELAY_ENABLE == true);
            BEAST_EXPECT(c.VP_REDUCE_RELAY_SQUELCH == true);

            Config c1;

            toLoad = (R"rippleConfig(
[reduce_relay]
vp_enable=0
vp_squelch=0
)rippleConfig");

            c1.loadFromString(toLoad);
            BEAST_EXPECT(c1.VP_REDUCE_RELAY_ENABLE == false);
            BEAST_EXPECT(c1.VP_REDUCE_RELAY_SQUELCH == false);

            Config c2;

            toLoad = R"rippleConfig(
[reduce_relay]
vp_enabled=1
vp_squelched=1
)rippleConfig";

            c2.loadFromString(toLoad);
            BEAST_EXPECT(c2.VP_REDUCE_RELAY_ENABLE == false);
            BEAST_EXPECT(c2.VP_REDUCE_RELAY_SQUELCH == false);
        });
    }

    void
    testInternalHashRouter(bool log)
    {
        doTest("Duplicate Message", log, [&](bool log) {
            network_.reset();
            // update message count for the same peer/validator
            std::int16_t nMessages = 5;
            for (int i = 0; i < nMessages; i++)
            {
                uint256 key(i);
                network_.overlay().updateSlotAndSquelch(
                    key,
                    network_.validator(0),
                    0,
                    [&](PublicKey const&, PeerWPtr, std::uint32_t) {});
            }
            auto peers = network_.overlay().getPeers(network_.validator(0));
            // first message changes Slot state to Counting and is not counted,
            // hence '-1'.
            BEAST_EXPECT(std::get<1>(peers[0]) == (nMessages - 1));
            // add duplicate
            uint256 key(nMessages - 1);
            network_.overlay().updateSlotAndSquelch(
                key,
                network_.validator(0),
                0,
                [&](PublicKey const&, PeerWPtr, std::uint32_t) {});
            // confirm the same number of messages
            peers = network_.overlay().getPeers(network_.validator(0));
            BEAST_EXPECT(std::get<1>(peers[0]) == (nMessages - 1));
            // advance the clock
            ManualClock::advance(reduce_relay::IDLED + seconds(1));
            network_.overlay().updateSlotAndSquelch(
                key,
                network_.validator(0),
                0,
                [&](PublicKey const&, PeerWPtr, std::uint32_t) {});
            peers = network_.overlay().getPeers(network_.validator(0));
            // confirm message number increased
            BEAST_EXPECT(std::get<1>(peers[0]) == nMessages);
        });
    }

    struct Handler : public reduce_relay::SquelchHandler
    {
        Handler() : maxDuration_(0)
        {
        }
        void
        squelch(PublicKey const&, Peer::id_t, std::uint32_t duration)
            const override
        {
            if (duration > maxDuration_)
                maxDuration_ = duration;
        }
        void
        unsquelch(PublicKey const&, Peer::id_t) const override
        {
        }
        mutable int maxDuration_;
    };

    void
    testRandomSquelch(bool l)
    {
        doTest("Random Squelch", l, [&](bool l) {
            PublicKey validator = std::get<0>(randomKeyPair(KeyType::ed25519));
            Handler handler;

            auto run = [&](int npeers) {
                handler.maxDuration_ = 0;
                reduce_relay::Slots<ManualClock> slots(env_.app(), handler);
                // 1st message from a new peer switches the slot
                // to counting state and resets the counts of all peers +
                // MAX_MESSAGE_THRESHOLD + 1 messages to reach the threshold
                // and switch the slot's state to peer selection.
                for (int m = 1; m <= reduce_relay::MAX_MESSAGE_THRESHOLD + 2;
                     m++)
                {
                    for (int peer = 0; peer < npeers; peer++)
                    {
                        // make unique message hash to make the
                        // slot's internal hash router accept the message
                        std::uint64_t mid = m * 1000 + peer;
                        uint256 const message{mid};
                        slots.updateSlotAndSquelch(
                            message,
                            validator,
                            peer,
                            protocol::MessageType::mtVALIDATION);
                    }
                }
                // make Slot's internal hash router expire all messages
                ManualClock::advance(hours(1));
            };

            using namespace reduce_relay;
            // expect max duration less than MAX_UNSQUELCH_EXPIRE_DEFAULT with
            // less than or equal to 60 peers
            run(20);
            BEAST_EXPECT(
                handler.maxDuration_ >= MIN_UNSQUELCH_EXPIRE.count() &&
                handler.maxDuration_ <= MAX_UNSQUELCH_EXPIRE_DEFAULT.count());
            run(60);
            BEAST_EXPECT(
                handler.maxDuration_ >= MIN_UNSQUELCH_EXPIRE.count() &&
                handler.maxDuration_ <= MAX_UNSQUELCH_EXPIRE_DEFAULT.count());
            // expect max duration greater than MIN_UNSQUELCH_EXPIRE and less
            // than MAX_UNSQUELCH_EXPIRE_PEERS with peers greater than 60
            // and less than 360
            run(350);
            // can't make this condition stronger. squelch
            // duration is probabilistic and max condition may still fail.
            // log when the value is low
            BEAST_EXPECT(
                handler.maxDuration_ >= MIN_UNSQUELCH_EXPIRE.count() &&
                handler.maxDuration_ <= MAX_UNSQUELCH_EXPIRE_PEERS.count());
            using namespace beast::unit_test::detail;
            if (handler.maxDuration_ <= MAX_UNSQUELCH_EXPIRE_DEFAULT.count())
                log << make_reason(
                           "warning: squelch duration is low",
                           __FILE__,
                           __LINE__)
                    << std::endl
                    << std::flush;
            // more than 400 is still less than MAX_UNSQUELCH_EXPIRE_PEERS
            run(400);
            BEAST_EXPECT(
                handler.maxDuration_ >= MIN_UNSQUELCH_EXPIRE.count() &&
                handler.maxDuration_ <= MAX_UNSQUELCH_EXPIRE_PEERS.count());
            if (handler.maxDuration_ <= MAX_UNSQUELCH_EXPIRE_DEFAULT.count())
                log << make_reason(
                           "warning: squelch duration is low",
                           __FILE__,
                           __LINE__)
                    << std::endl
                    << std::flush;
        });
    }

    void
    testHandshake(bool log)
    {
        doTest("Handshake", log, [&](bool log) {
            auto setEnv = [&](bool enable) {
                Config c;
                std::stringstream str;
                str << "[reduce_relay]\n"
                    << "vp_enable=" << enable << "\n"
                    << "vp_squelch=" << enable << "\n"
                    << "[compression]\n"
                    << "1\n";
                c.loadFromString(str.str());
                env_.app().config().VP_REDUCE_RELAY_ENABLE =
                    c.VP_REDUCE_RELAY_ENABLE;
                env_.app().config().VP_REDUCE_RELAY_SQUELCH =
                    c.VP_REDUCE_RELAY_SQUELCH;
                env_.app().config().COMPRESSION = c.COMPRESSION;
            };
            auto handshake = [&](int outboundEnable, int inboundEnable) {
                beast::IP::Address addr =
                    boost::asio::ip::address::from_string("172.1.1.100");

                setEnv(outboundEnable);
                auto request = ripple::makeRequest(
                    true,
                    env_.app().config().COMPRESSION,
                    env_.app().config().VP_REDUCE_RELAY_ENABLE,
                    false);
                http_request_type http_request;
                http_request.version(request.version());
                http_request.base() = request.base();
                // feature enabled on the peer's connection only if both sides
                // are enabled
                auto const peerEnabled = inboundEnable && outboundEnable;
                // inbound is enabled if the request's header has the feature
                // enabled and the peer's configuration is enabled
                auto const inboundEnabled = peerFeatureEnabled(
                    http_request, FEATURE_VPRR, inboundEnable);
                BEAST_EXPECT(!(peerEnabled ^ inboundEnabled));

                setEnv(inboundEnable);
                auto http_resp = ripple::makeResponse(
                    true,
                    http_request,
                    addr,
                    addr,
                    uint256{1},
                    1,
                    {1, 0},
                    env_.app());
                // outbound is enabled if the response's header has the feature
                // enabled and the peer's configuration is enabled
                auto const outboundEnabled =
                    peerFeatureEnabled(http_resp, FEATURE_VPRR, outboundEnable);
                BEAST_EXPECT(!(peerEnabled ^ outboundEnabled));
            };
            handshake(1, 1);
            handshake(1, 0);
            handshake(0, 1);
            handshake(0, 0);
        });
    }

    jtx::Env env_;
    Network network_;

public:
    reduce_relay_test() : env_(*this), network_(env_.app())
    {
    }

    void
    run() override
    {
        bool log = false;
        testConfig(log);
        testInitialRound(log);
        testPeerUnsquelchedTooSoon(log);
        testPeerUnsquelched(log);
        testNewPeer(log);
        testSquelchedPeerDisconnects(log);
        testSelectedPeerDisconnects(log);
        testSelectedPeerStopsRelaying(log);
        testInternalHashRouter(log);
        testRandomSquelch(log);
        testHandshake(log);
    }
};

class reduce_relay_simulate_test : public reduce_relay_test
{
    void
    testRandom(bool log)
    {
        doTest("Random Test", log, [&](bool log) { random(log); });
    }

    void
    run() override
    {
        bool log = false;
        testRandom(log);
    }
};

BEAST_DEFINE_TESTSUITE(reduce_relay, ripple_data, ripple);
BEAST_DEFINE_TESTSUITE_MANUAL(reduce_relay_simulate, ripple_data, ripple);

}  // namespace test

}  // namespace ripple