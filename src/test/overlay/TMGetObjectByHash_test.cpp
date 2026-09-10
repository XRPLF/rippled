#include <test/jtx/Env.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/overlay/detail/Tuning.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/Handoff.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

using namespace jtx;

/**
 * Test for TMGetObjectByHash reply size limiting.
 *
 * This verifies the fix that limits TMGetObjectByHash replies to
 * tuning::hardMaxReplyNodes to prevent excessive memory usage and
 * potential DoS attacks from peers requesting large numbers of objects.
 */
class TMGetObjectByHash_test : public beast::unit_test::Suite
{
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using socket_type = boost::asio::ip::tcp::socket;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;
    /**
     * Test peer that captures sent messages for verification.
     */
    class PeerTest : public PeerImp
    {
    public:
        PeerTest(
            Application& app,
            std::shared_ptr<peer_finder::Slot> const& slot,
            http_request_type&& request,
            PublicKey const& publicKey,
            ProtocolVersion protocol,
            resource::Consumer consumer,
            std::unique_ptr<TMGetObjectByHash_test::stream_type>&& streamPtr,
            OverlayImpl& overlay)
            : PeerImp(
                  app,
                  id++,
                  slot,
                  std::move(request),
                  publicKey,
                  protocol,
                  consumer,
                  std::move(streamPtr),
                  overlay)
        {
        }

        ~PeerTest() override = default;

        void
        run() override
        {
        }

        void
        send(std::shared_ptr<Message> const& m) override
        {
            lastSentMessage_ = m;
        }

        std::shared_ptr<Message>
        getLastSentMessage() const
        {
            return lastSentMessage_;
        }

        /**
         * Capture the charge the handler applies, then apply it for real.
         *
         * `Peer::charge()` is pure virtual and `processGetObjectByHash()`
         * calls it unqualified, so this override sees the exact
         * `resource::Charge` the handler built -- the same object passed to
         * `computeGetObjectByHashFee()`'s caller. That makes the handler's
         * *choice of argument* observable, which reading `fee_` or calling
         * the pricing helper with the test's own arguments cannot do.
         *
         * Recorded before forwarding so the base-class strand hop cannot
         * reorder the observation; forwarding keeps the production
         * disconnect/accounting behaviour intact.
         */
        void
        charge(resource::Charge const& fee, std::string const& context) override
        {
            lastAppliedCharge_ = fee;
            lastChargeContext_ = context;
            PeerImp::charge(fee, context);
        }

        /**
         * The charge captured by the override above, or nullopt if none.
         *
         * `resource::Charge` has no default constructor, so the optional
         * also distinguishes "not charged at all" from "charged zero" --
         * a distinction the rejection-gate tests depend on.
         */
        [[nodiscard]] std::optional<resource::Charge> const&
        getLastAppliedCharge() const
        {
            return lastAppliedCharge_;
        }

        /**
         * The context string that accompanied the captured charge.
         */
        [[nodiscard]] std::string const&
        getLastChargeContext() const
        {
            return lastChargeContext_;
        }

        // Synchronous test access to the JobQueue-dispatched processor.
        // The production path runs this on JtLedgerReq; tests need a
        // synchronous entry point to inspect the reply via send().
        // PeerImp::processGetObjectByHash is `protected` so the derived
        // test subclass can call it directly.
        void
        runProcessGetObjectByHash(std::shared_ptr<protocol::TMGetObjectByHash> const& m)
        {
            processGetObjectByHash(m);
        }

        // Read the accumulated per-message charge. `currentFeeCharge()` is
        // protected on PeerImp; exposed here because it is the one
        // deterministic, same-thread witness that a rejection gate fired --
        // `charge()` itself dispatches to the peer's strand.
        [[nodiscard]] resource::Charge
        peekFeeCharge() const
        {
            return currentFeeCharge();
        }

        // The differential-pricing helper, so a test can compare the charge
        // applied by the handler against the helper's own result for the
        // same inputs. Static and protected on PeerImp.
        [[nodiscard]] static resource::Charge
        peekComputeFee(int const requested, int const found)
        {
            return computeGetObjectByHashFee(requested, found);
        }

        static void
        resetId()
        {
            id = 0;
        }

    private:
        inline static Peer::id_t id = 0;

        /**
         * The last message handed to send().
         *
         * @note Not synchronised. When the handler runs on a JobQueue
         * worker (the `onMessage()` tests), this is written on that worker
         * and read on the test thread, so every such test must call
         * `env.app().getJobQueue().rendezvous()` before reading it. The
         * rendezvous supplies the happens-before edge: the worker's
         * `--processCount_` under `mutex_` in `JobQueue::processTask()`
         * releases, and the waiter's predicate acquires the same mutex.
         * Tests that drive `runProcessGetObjectByHash()` directly run
         * wholly on the test thread and need no rendezvous.
         */
        std::shared_ptr<Message> lastSentMessage_;

        /**
         * @see getLastAppliedCharge(). Same threading rules as above.
         */
        std::optional<resource::Charge> lastAppliedCharge_;

        /**
         * @see getLastChargeContext(). Same threading rules as above.
         */
        std::string lastChargeContext_;
    };

    shared_context context_{makeSslContext("")};
    ProtocolVersion protocolVersion_{1, 7};

    /**
     * Seed offset for hashes that must NOT be present in the NodeStore.
     *
     * `createRequest()` stores `sha512Half(i)` for i in [0, numObjects), and
     * numObjects can reach kHardMaxReplyNodes. Offsetting well past that
     * keeps "unstored" hashes genuinely absent.
     */
    static constexpr int kUnstoredHashSeed = 1'000'000;

    /**
     * Build a live PeerTest registered with the overlay.
     *
     * @note `overlay.addActive()` stores only `std::weak_ptr`s
     * (`OverlayImpl::peers_`, `ids_` and `list_` are all weak), so it does
     * *not* keep the peer alive. The returned `shared_ptr` is the sole
     * owner; keep it in scope for the whole test. Safety for the
     * JobQueue-dispatched path comes from the job lambda locking its own
     * `weak_ptr` plus the `rendezvous()` each such test performs.
     */
    std::shared_ptr<PeerTest>
    createPeer(jtx::Env& env)
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        boost::beast::http::request<boost::beast::http::dynamic_body> request;
        auto streamPtr =
            std::make_unique<stream_type>(socket_type(env.app().getIOContext()), *context_);

        beast::ip::Endpoint const local(boost::asio::ip::make_address("172.1.1.1"), 51235);
        beast::ip::Endpoint const remote(boost::asio::ip::make_address("172.1.1.2"), 51235);

        PublicKey const key(std::get<0>(randomKeyPair(KeyType::Ed25519)));
        auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto [slot, _] = overlay.peerFinder().newInboundSlot(local, remote);

        auto peer = std::make_shared<PeerTest>(
            env.app(),
            slot,
            std::move(request),
            key,
            protocolVersion_,
            consumer,
            std::move(streamPtr),
            overlay);

        overlay.addActive(peer);
        return peer;
    }

    static std::shared_ptr<protocol::TMGetObjectByHash>
    createRequest(size_t const numObjects, Env& env)
    {
        // Store objects in the NodeStore that will be found during the query
        auto& nodeStore = env.app().getNodeStore();

        // Create and store objects
        std::vector<uint256> hashes;
        hashes.reserve(numObjects);
        for (int i = 0; i < numObjects; ++i)
        {
            uint256 const hash(xrpl::sha512Half(i));
            hashes.push_back(hash);

            Blob data(100, static_cast<unsigned char>(i % 256));
            nodeStore.store(
                NodeObjectType::Ledger, std::move(data), hash, nodeStore.earliestLedgerSeq());
        }

        // Create a request with more objects than hardMaxReplyNodes
        auto request = std::make_shared<protocol::TMGetObjectByHash>();
        request->set_type(protocol::TMGetObjectByHash_ObjectType_otLEDGER);
        request->set_query(true);

        for (int i = 0; i < numObjects; ++i)
        {
            auto object = request->add_objects();
            object->set_hash(hashes[i].data(), hashes[i].size());
            object->set_ledgerseq(i);
        }
        return request;
    }

    /**
     * Parse the reply captured by PeerTest::send().
     *
     * @return The decoded message, or std::nullopt when nothing was sent.
     */
    std::optional<protocol::TMGetObjectByHash>
    parseReply(std::shared_ptr<PeerTest> const& peer)
    {
        auto const sentMessage = peer->getLastSentMessage();
        if (!sentMessage)
            return std::nullopt;

        auto const& buffer = sentMessage->getBuffer(compression::Compressed::Off);
        BEAST_EXPECT(buffer.size() > 6);

        // Skip the 6-byte message header (4 size + 2 type).
        protocol::TMGetObjectByHash reply;
        BEAST_EXPECT(reply.ParseFromArray(buffer.data() + 6, buffer.size() - 6) == true);
        return reply;
    }

    /**
     * Test that reply is limited to hardMaxReplyNodes when more objects
     * are requested than the limit allows.
     *
     * `onMessage(TMGetObjectByHash)` dispatches the generic-query path
     * to the JobQueue, so tests invoke the synchronous processor
     * directly via `runProcessGetObjectByHash`.
     */
    void
    testReplyLimit(size_t const numObjects, int const expectedReplySize)
    {
        testcase("Reply Limit");

        Env env(*this);
        PeerTest::resetId();

        auto peer = createPeer(env);

        auto request = createRequest(numObjects, env);
        peer->runProcessGetObjectByHash(request);

        // Verify that a reply was sent
        auto reply = parseReply(peer);
        BEAST_EXPECT(reply.has_value());
        if (!reply)
            return;

        // Verify the reply is limited to expectedReplySize
        BEAST_EXPECT(reply->objects_size() == expectedReplySize);
    }

    //--------------------------------------------------------------------------
    // Request-gate rejection paths
    //--------------------------------------------------------------------------

    /**
     * Build a query request with @p numObjects hashes and nothing stored.
     *
     * Distinct from `createRequest()`, which writes every hash to the
     * NodeStore. The rejection gates return before any NodeStore access, so
     * storing 12289 objects to test them would cost real time and prove
     * nothing. Hashes are derived from the index but need not resolve.
     *
     * @param numObjects  Objects to place in the request.
     * @param type        Message type; must not be otFETCH_PACK or
     *                    otTRANSACTIONS, both of which are intercepted by
     *                    earlier branches of onMessage().
     */
    static std::shared_ptr<protocol::TMGetObjectByHash>
    createUnstoredRequest(
        int const numObjects,
        protocol::TMGetObjectByHash::ObjectType const type =
            protocol::TMGetObjectByHash_ObjectType_otLEDGER)
    {
        auto request = std::make_shared<protocol::TMGetObjectByHash>();
        request->set_type(type);
        request->set_query(true);

        for (int i = 0; i < numObjects; ++i)
        {
            // Offset the seed so these hashes cannot collide with the ones
            // createRequest() stores, keeping "unstored" unambiguous.
            uint256 const hash(xrpl::sha512Half(i + kUnstoredHashSeed));
            auto* object = request->add_objects();
            object->set_hash(hash.data(), hash.size());
        }
        return request;
    }

    /**
     * An oversized request is refused with no reply and an exact fee.
     *
     * This is the gate that `getobject_rejected_total{reason="oversize"}`
     * counts. The counter itself is not readable in-process (see the note
     * on run()), so the assertions are on the two observable effects of the
     * same early return: no message was sent, and `fee_` holds exactly
     * `kFeeInvalidData`.
     */
    void
    testOversizeRejection()
    {
        testcase("Oversize Rejection");

        Env env(*this);
        PeerTest::resetId();
        auto peer = createPeer(env);

        // Successful-setup assertion: a fresh peer starts at the trivial
        // fee, so the post-condition below can only come from this call.
        BEAST_EXPECT(peer->peekFeeCharge().cost() == resource::kFeeTrivialPeer.cost());
        BEAST_EXPECT(peer->getLastSentMessage() == nullptr);

        int const oversize = static_cast<int>(tuning::kHardMaxReplyNodes) + 1;
        peer->onMessage(createUnstoredRequest(oversize));

        // State: nothing was replied to, because the gate returns before
        // the job is queued.
        BEAST_EXPECT(peer->getLastSentMessage() == nullptr);

        // Cause: the charge is exactly the invalid-data fee, not merely
        // "some larger fee". The label pins which gate fired -- the
        // malformed-ledgerhash gate charges a different constant.
        BEAST_EXPECT(peer->peekFeeCharge().cost() == resource::kFeeInvalidData.cost());
        BEAST_EXPECT(peer->peekFeeCharge().cost() == 400);
        BEAST_EXPECT(peer->peekFeeCharge().label() == resource::kFeeInvalidData.label());

        // Negative path for the differential charge: the gate returns before
        // the handler runs, so computeGetObjectByHashFee() is never reached
        // and charge() is never called. Nothing was applied at all -- which
        // the optional distinguishes from a zero-cost charge.
        BEAST_EXPECT(!peer->getLastAppliedCharge().has_value());
    }

    /**
     * Exactly at the limit the request is accepted, so the gate is a strict
     * `>` and not `>=`.
     *
     * Negative control for testOversizeRejection: without it, a gate that
     * rejected everything would pass that test.
     */
    void
    testAtLimitNotRejected()
    {
        testcase("At Limit Not Rejected");

        Env env(*this);
        PeerTest::resetId();
        auto peer = createPeer(env);

        int const atLimit = static_cast<int>(tuning::kHardMaxReplyNodes);
        peer->onMessage(createUnstoredRequest(atLimit));

        // Accepted: the request was queued, so the fee is the
        // moderate-burden admission charge, not the invalid-data charge.
        //
        // `fee_.update()` for the admission charge runs on this thread, but
        // the enqueued worker also writes the peer's reply. Drain the queue
        // before reading anything so the observation cannot race the worker.
        env.app().getJobQueue().rendezvous();

        BEAST_EXPECT(peer->peekFeeCharge().cost() == resource::kFeeModerateBurdenPeer.cost());
        BEAST_EXPECT(peer->peekFeeCharge().cost() != resource::kFeeInvalidData.cost());
        BEAST_EXPECT(peer->peekFeeCharge().cost() == 250);

        // The request was in bounds, so the worker ran and replied. Nothing
        // was stored, so every lookup missed and the reply is empty.
        auto reply = parseReply(peer);
        BEAST_EXPECT(reply.has_value());
        if (reply)
        {
            BEAST_EXPECT(reply->objects_size() == 0);
        }

        // Positive counterpart to the oversize test: because the handler did
        // run, a differential charge was applied, and it is exactly the
        // all-miss price for the full request size.
        auto const& applied = peer->getLastAppliedCharge();
        BEAST_EXPECT(applied.has_value());
        if (applied)
        {
            BEAST_EXPECT(applied->cost() == PeerTest::peekComputeFee(atLimit, 0).cost());
            BEAST_EXPECT(applied->cost() == 99176);
        }
    }

    /**
     * A wrong-sized ledgerhash is refused with no reply and an exact fee.
     *
     * This is the gate `getobject_rejected_total{reason="malformed_ledgerhash"}`
     * counts. `stringIsUInt256Sized` requires exactly `uint256::size()`
     * bytes, so both a short and a long hash must be refused; a test using
     * only one would miss an off-by-one in either direction.
     *
     * @param hashSize  Byte length of the malformed ledgerhash.
     */
    void
    testMalformedLedgerHashRejection(std::size_t const hashSize)
    {
        testcase("Malformed LedgerHash Rejection: " + std::to_string(hashSize) + " bytes");

        BEAST_EXPECT(hashSize != uint256::size());

        Env env(*this);
        PeerTest::resetId();
        auto peer = createPeer(env);

        BEAST_EXPECT(peer->peekFeeCharge().cost() == resource::kFeeTrivialPeer.cost());

        // Small in-bounds object count, so this can only be the ledgerhash
        // gate: the oversize gate is checked afterwards and cannot fire.
        auto request = createUnstoredRequest(1);
        request->set_ledgerhash(std::string(hashSize, 'x'));
        peer->onMessage(request);

        BEAST_EXPECT(peer->getLastSentMessage() == nullptr);
        BEAST_EXPECT(peer->peekFeeCharge().cost() == resource::kFeeMalformedRequest.cost());
        BEAST_EXPECT(peer->peekFeeCharge().cost() == 200);
        BEAST_EXPECT(peer->peekFeeCharge().label() == resource::kFeeMalformedRequest.label());

        // Negative path: the gate returns before the handler, so no
        // differential charge was ever applied.
        BEAST_EXPECT(!peer->getLastAppliedCharge().has_value());
    }

    /**
     * A correctly sized ledgerhash passes the gate.
     *
     * Negative control for testMalformedLedgerHashRejection.
     */
    void
    testWellFormedLedgerHashAccepted()
    {
        testcase("Well-Formed LedgerHash Accepted");

        Env env(*this);
        PeerTest::resetId();
        auto peer = createPeer(env);

        auto request = createUnstoredRequest(1);
        uint256 const ledgerHash(xrpl::sha512Half(0));
        BEAST_EXPECT(ledgerHash.size() == uint256::size());
        request->set_ledgerhash(ledgerHash.data(), ledgerHash.size());
        peer->onMessage(request);

        // Drain the enqueued worker before observing, as in
        // testAtLimitNotRejected.
        env.app().getJobQueue().rendezvous();

        // Not the malformed charge: the request was admitted.
        BEAST_EXPECT(peer->peekFeeCharge().cost() == resource::kFeeModerateBurdenPeer.cost());
        BEAST_EXPECT(peer->peekFeeCharge().cost() != resource::kFeeMalformedRequest.cost());
        BEAST_EXPECT(peer->peekFeeCharge().cost() == 250);

        // A reply was produced, and it echoes the request's ledgerhash.
        auto reply = parseReply(peer);
        BEAST_EXPECT(reply.has_value());
        if (reply)
        {
            BEAST_EXPECT(reply->has_ledgerhash());
            BEAST_EXPECT(reply->ledgerhash() == request->ledgerhash());
        }
    }

    //--------------------------------------------------------------------------
    // Hit / miss split and charge
    //--------------------------------------------------------------------------

    /**
     * Build a request that interleaves stored and unstored hashes.
     *
     * One of each is taken in turn until a side runs out, then whichever
     * side remains is drained. Interleaving matters: a handler that stopped
     * at the first miss would return fewer objects than expected, which a
     * stored-then-unstored layout could hide.
     *
     * Asserts on its own setup, so a miscount here is reported at this call
     * rather than as a confusing reply-size failure later.
     *
     * @param env           Environment whose NodeStore receives the writes.
     * @param numStored     Hashes written to the NodeStore, i.e. hits.
     * @param numUnstored   Hashes left absent, i.e. misses.
     * @param storedHashes  Out-param populated with the stored hashes.
     * @return The assembled request.
     */
    std::shared_ptr<protocol::TMGetObjectByHash>
    buildInterleavedRequest(
        Env& env,
        int const numStored,
        int const numUnstored,
        std::set<uint256>& storedHashes)
    {
        auto& nodeStore = env.app().getNodeStore();

        auto request = std::make_shared<protocol::TMGetObjectByHash>();
        request->set_type(protocol::TMGetObjectByHash_ObjectType_otLEDGER);
        request->set_query(true);

        int stored = 0;
        int unstored = 0;
        for (int i = 0; i < numStored + numUnstored; ++i)
        {
            // Alternate while both remain; then drain whichever is left.
            bool const takeStored =
                (stored < numStored) && (unstored >= numUnstored || (i % 2) == 0);

            uint256 const hash(
                xrpl::sha512Half(takeStored ? stored : unstored + kUnstoredHashSeed));

            if (takeStored)
            {
                Blob data(100, static_cast<unsigned char>(stored % 256));
                nodeStore.store(
                    NodeObjectType::Ledger, std::move(data), hash, nodeStore.earliestLedgerSeq());
                BEAST_EXPECT(storedHashes.insert(hash).second);
                ++stored;
            }
            else
            {
                ++unstored;
            }

            auto* object = request->add_objects();
            object->set_hash(hash.data(), hash.size());
        }

        // Setup assertions: the mix is exactly what was asked for.
        BEAST_EXPECT(stored == numStored);
        BEAST_EXPECT(unstored == numUnstored);
        BEAST_EXPECT(storedHashes.size() == static_cast<std::size_t>(numStored));
        BEAST_EXPECT(request->objects_size() == numStored + numUnstored);

        return request;
    }

    /**
     * Every replied object is a distinct hash drawn from @p storedHashes.
     *
     * Without the distinctness check a handler that returned the same hit
     * twice would still satisfy a reply-size assertion.
     *
     * @param reply         The decoded reply.
     * @param storedHashes  The hashes that were written to the NodeStore.
     * @param numStored     Expected number of distinct returned hashes.
     */
    void
    verifyReplyObjects(
        protocol::TMGetObjectByHash const& reply,
        std::set<uint256> const& storedHashes,
        int const numStored)
    {
        std::set<uint256> returned;
        for (int i = 0; i < reply.objects_size(); ++i)
        {
            auto const& obj = reply.objects(i);
            BEAST_EXPECT(obj.hash().size() == uint256::size());
            BEAST_EXPECT(returned.insert(uint256::fromRaw(obj.hash())).second);
            BEAST_EXPECT(storedHashes.contains(uint256::fromRaw(obj.hash())));
        }
        BEAST_EXPECT(returned.size() == static_cast<std::size_t>(numStored));
    }

    /**
     * A mixed request returns exactly the stored objects and nothing else.
     *
     * This is the split `getobject_lookups_total{result=hit|miss}` records:
     * the handler derives the miss count as `requested - found`, so an
     * exact reply size is exactly the hit count the metric would report.
     *
     * @param numStored    Hashes written to the NodeStore before the call.
     * @param numUnstored  Hashes that will miss.
     */
    void
    testHitMissSplit(int const numStored, int const numUnstored)
    {
        testcase(
            "Hit/Miss Split: " + std::to_string(numStored) + " stored, " +
            std::to_string(numUnstored) + " unstored");

        Env env(*this);
        PeerTest::resetId();
        auto peer = createPeer(env);

        std::set<uint256> storedHashes;
        auto request = buildInterleavedRequest(env, numStored, numUnstored, storedHashes);

        int const requested = numStored + numUnstored;
        peer->runProcessGetObjectByHash(request);

        auto reply = parseReply(peer);
        BEAST_EXPECT(reply.has_value());
        if (!reply)
            return;

        // The exact hit count. Every stored hash is returned and no
        // unstored one is, so hits == numStored and the derived miss count
        // is exactly numUnstored.
        BEAST_EXPECT(reply->objects_size() == numStored);
        BEAST_EXPECT(requested - reply->objects_size() == numUnstored);

        verifyReplyObjects(*reply, storedHashes, numStored);

        // Cause: the value recorded as getobject_charge is exactly the
        // charge the handler applied, captured by PeerTest::charge().
        auto const& applied = peer->getLastAppliedCharge();
        BEAST_EXPECT(applied.has_value());
        if (!applied)
            return;
        BEAST_EXPECT(applied->cost() == PeerTest::peekComputeFee(requested, numStored).cost());
        BEAST_EXPECT(applied->label() == "GetObject differential");

        // These request sizes are all within kFreeObjectsPerRequest, so the
        // charge is exactly zero regardless of the split. Asserted rather
        // than assumed: it is why this test does not also pin a non-trivial
        // fee -- testComputeFeeExactValues covers the billable bands.
        BEAST_EXPECT(requested <= static_cast<int>(tuning::kFreeObjectsPerRequest));
        BEAST_EXPECT(applied->cost() == 0);

        // `fee_` is untouched on this path: the handler charges through
        // charge(), never through fee_.update().
        BEAST_EXPECT(peer->peekFeeCharge().cost() == resource::kFeeTrivialPeer.cost());
    }

    /**
     * One pricing case: inputs, the derived expectation, and the literal.
     *
     * Both expectations are kept. `derived` is written from the Tuning
     * constants so a deliberate re-pricing needs one edit; `literal` pins the
     * number those constants currently produce, so a re-pricing cannot pass
     * unnoticed by being self-consistently wrong.
     */
    struct FeeCase
    {
        /**
         * Objects the peer asked for.
         */
        int requested;
        /**
         * Objects that resolved in the NodeStore.
         */
        int found;
        /**
         * Expectation computed from the Tuning constants.
         */
        int derived;
        /**
         * The same value as a hard number.
         */
        int literal;
        /**
         * Reported when the case fails.
         */
        char const* why;
    };

    /**
     * Assert computeGetObjectByHashFee() equals both expectations of a case.
     *
     * @param fc The case to check.
     */
    void
    checkFeeCase(FeeCase const& fc)
    {
        auto const cost = PeerTest::peekComputeFee(fc.requested, fc.found).cost();
        BEAST_EXPECTS(cost == fc.derived, fc.why);
        BEAST_EXPECTS(cost == fc.literal, fc.why);
    }

    /**
     * The Tuning constants the fee expectations are built from.
     *
     * Verified against their literal values by testComputeFeeExactValues()
     * so a silent re-pricing shows up as a failure there rather than as a
     * self-consistent but wrong expectation in every case below.
     */
    struct FeeConstants
    {
        int free{static_cast<int>(tuning::kFreeObjectsPerRequest)};
        int hit{static_cast<int>(tuning::kCostPerLookupHit)};
        int miss{static_cast<int>(tuning::kCostPerLookupMiss)};
        int bandSmall{static_cast<int>(tuning::kCostBandSmall)};
        int bandMedium{static_cast<int>(tuning::kCostBandMedium)};
        int bandLarge{static_cast<int>(tuning::kCostBandLarge)};
        int smallMax{static_cast<int>(tuning::kBandSmallMax)};
        int mediumMax{static_cast<int>(tuning::kBandMediumMax)};
    };

    /**
     * Every pricing case, in one table.
     *
     * @param k The Tuning constants to derive expectations from.
     */
    static std::vector<FeeCase>
    makeFeeCases(FeeConstants const& k)
    {
        return {
            // Wholly free: at or below the free allowance nothing is
            // billable, so only the small size band applies -- which is 0.
            {.requested = k.free,
             .found = k.free,
             .derived = k.bandSmall,
             .literal = 0,
             .why = "at the free allowance"},
            {.requested = 0,
             .found = 0,
             .derived = k.bandSmall,
             .literal = 0,
             .why = "empty request"},
            {.requested = 1,
             .found = 1,
             .derived = k.bandSmall,
             .literal = 0,
             .why = "one object, one hit"},

            // All hits, one object past the allowance: one billable hit.
            {.requested = k.free + 1,
             .found = k.free + 1,
             .derived = k.hit + k.bandSmall,
             .literal = 1,
             .why = "one billable hit"},

            // All misses, one past the allowance: misses are billed first,
            // so the single billable object is priced as a miss, not a hit.
            {.requested = k.free + 1,
             .found = 0,
             .derived = k.miss + k.bandSmall,
             .literal = 8,
             .why = "one billable miss"},

            // Mixed at the small-band edge: 64 requested, 32 found.
            // Billable is 64-16 = 48; misses are 32 and all billable,
            // leaving 16 billable hits.
            {.requested = k.smallMax,
             .found = 32,
             .derived = (16 * k.hit) + (32 * k.miss) + k.bandSmall,
             .literal = 272,
             .why = "small-band edge, mixed"},

            // One past the small band moves to the medium surcharge.
            {.requested = k.smallMax + 1,
             .found = k.smallMax + 1,
             .derived = ((k.smallMax + 1 - k.free) * k.hit) + k.bandMedium,
             .literal = 149,
             .why = "first medium-band size"},

            // The medium band's last size, then one past it, which moves to
            // the large surcharge.
            {.requested = k.mediumMax,
             .found = k.mediumMax,
             .derived = ((k.mediumMax - k.free) * k.hit) + k.bandMedium,
             .literal = 1108,
             .why = "last medium-band size"},
            {.requested = k.mediumMax + 1,
             .found = k.mediumMax + 1,
             .derived = ((k.mediumMax + 1 - k.free) * k.hit) + k.bandLarge,
             .literal = 2009,
             .why = "first large-band size"},

            // Clamp: found > requested cannot make the miss count negative,
            // so the fee is the same as the all-hit case (1).
            {.requested = k.free + 1,
             .found = k.free + 10,
             .derived = k.hit + k.bandSmall,
             .literal = 1,
             .why = "found exceeds requested"},
        };
    }

    /**
     * computeGetObjectByHashFee() returns exactly the documented value.
     *
     * The metric records the helper's result verbatim, so pinning the helper
     * pins what `getobject_charge` reports.
     */
    void
    testComputeFeeExactValues()
    {
        testcase("Compute Fee Exact Values");

        FeeConstants const k;

        // Verify the constants themselves, so a silent re-pricing shows up
        // here rather than as a self-consistent but wrong expectation.
        BEAST_EXPECT(k.free == 16);
        BEAST_EXPECT(k.hit == 1);
        BEAST_EXPECT(k.miss == 8);
        BEAST_EXPECT(k.bandSmall == 0);
        BEAST_EXPECT(k.bandMedium == 100);
        BEAST_EXPECT(k.bandLarge == 1000);
        BEAST_EXPECT(k.smallMax == 64);
        BEAST_EXPECT(k.mediumMax == 1024);

        auto const cases = makeFeeCases(k);
        BEAST_EXPECT(cases.size() == 10);
        for (auto const& fc : cases)
            checkFeeCase(fc);

        // A miss costs strictly more than a hit for the same request size.
        // Relational, so it cannot be expressed as a table row.
        BEAST_EXPECT(
            PeerTest::peekComputeFee(k.free + 1, 0).cost() >
            PeerTest::peekComputeFee(k.free + 1, k.free + 1).cost());

        // The label is fixed, so a charge can be attributed to this helper.
        BEAST_EXPECT(PeerTest::peekComputeFee(k.free + 1, 0).label() == "GetObject differential");
    }

    /**
     * The charge the handler *applies* is priced on `requested`, not on the
     * capped iteration count and not on `found`.
     *
     * Load-bearing because `getobject_charge` records the applied value: if
     * `processGetObjectByHash()` priced on `iterLimit` -- i.e.
     * `min(requested, kHardMaxReplyNodes)` -- the metric would under-report
     * abusive batches by exactly the overshoot.
     *
     * The assertion is on `PeerTest::charge()`, which overrides the virtual
     * the handler calls, so it observes the very `resource::Charge` object
     * the handler constructed. Two other candidate witnesses were rejected:
     *   - `fee_` / `peekFeeCharge()`: this path never touches `fee_`, it
     *     goes through `charge()`.
     *   - `usage_.balance()`: `Entry::add()` returns
     *     `localBalance.add(...) + remoteBalance` and `DecayingSample::add()`
     *     returns `value_ / Window` with `Window == kDecayWindowSeconds ==
     *     32`, so the balance is the applied cost divided by 32 with integer
     *     truncation. 99184 / 32 and 99176 / 32 are both 3099, so the
     *     balance cannot distinguish the mutation this test exists to catch.
     *     It also decays with wall-clock time (`BasicSecondsClock`), making
     *     any exact expectation racy.
     */
    void
    testChargeUsesRequestedCount()
    {
        testcase("Charge Uses Requested Count");

        Env env(*this);
        PeerTest::resetId();
        auto peer = createPeer(env);

        int const requested = static_cast<int>(tuning::kHardMaxReplyNodes) + 1;
        int const capped = static_cast<int>(tuning::kHardMaxReplyNodes);

        // Successful-setup assertion: nothing has been charged yet, so the
        // post-condition below can only come from the handler call.
        BEAST_EXPECT(!peer->getLastAppliedCharge().has_value());

        // No hashes are stored, so every lookup misses and `found` is 0.
        // Called directly, so the handler and the charge both run on this
        // thread: `charge()` dispatches to strand_, and boost's strand
        // `dispatch` runs the function inline when the caller is not already
        // in the strand and the strand is idle. The capture in the override
        // happens before that hop regardless, so the observation is
        // deterministic either way.
        peer->runProcessGetObjectByHash(createUnstoredRequest(requested));

        auto reply = parseReply(peer);
        BEAST_EXPECT(reply.has_value());
        if (!reply)
            return;
        BEAST_EXPECT(reply->objects_size() == 0);

        // State: a charge was applied at all.
        auto const& applied = peer->getLastAppliedCharge();
        BEAST_EXPECT(applied.has_value());
        if (!applied)
            return;

        // Cause: it is exactly the requested-count price. This is the
        // assertion the test is named for. Under either plausible
        // mis-pricing it reads a different number and therefore fails:
        //   priced on `iterLimit` (12288) -> 99176
        //   priced on `found`        (0)  -> 0, since billable clamps to 0
        //                                   and the band drops to Small
        BEAST_EXPECT(applied->cost() == 99184);
        BEAST_EXPECT(applied->cost() == PeerTest::peekComputeFee(requested, 0).cost());
        BEAST_EXPECT(applied->cost() != PeerTest::peekComputeFee(capped, 0).cost());

        // Attribution: the charge came from the differential helper, not
        // from one of the flat admission or rejection constants.
        BEAST_EXPECT(applied->label() == "GetObject differential");
        BEAST_EXPECT(peer->getLastChargeContext() == "processed get object by hash request");

        // `fee_` is untouched on this path, which is why the override above
        // exists rather than a peekFeeCharge() assertion.
        BEAST_EXPECT(peer->peekFeeCharge().cost() == resource::kFeeTrivialPeer.cost());

        // Pricing on the requested count is strictly more expensive than
        // pricing on the capped count, which is what makes the choice
        // observable at all.
        BEAST_EXPECT(
            PeerTest::peekComputeFee(requested, 0).cost() >
            PeerTest::peekComputeFee(capped, 0).cost());

        // Exact values for both, so a change to either input is caught.
        BEAST_EXPECT(PeerTest::peekComputeFee(requested, 0).cost() == 99184);
        BEAST_EXPECT(PeerTest::peekComputeFee(capped, 0).cost() == 99176);
    }

    void
    run() override
    {
        // NOTE ON METRIC COVERAGE. The five getobject_* instruments are
        // recorded through the XRPL_METRIC_* macros, which push into the
        // OpenTelemetry SDK. That API is write-only by design -- there is no
        // read-back accessor and no in-memory metric reader in this build --
        // and a default jtx::Env leaves telemetry disabled, so the macros do
        // not execute at all here. These tests therefore assert the
        // observable behaviour of each instrumented code path, which pins
        // the values the instruments are fed:
        //   getobject_request_objects  <- the request's objects_size()
        //   getobject_lookups_total    <- reply size (hits) and the derived
        //                                 miss count, per testHitMissSplit
        //   getobject_charge           <- the applied resource::Charge,
        //                                 captured by PeerTest::charge()
        //   getobject_rejected_total   <- the two gates' exact fee_ values
        //                                 plus "no charge was applied"
        // Only getobject_lookup_us has no in-process witness, being a wall
        // clock reading. The counter and histogram values themselves remain
        // unverified by unit test and are checked live against Prometheus
        // per the design's live-validation step.
        int const limit = static_cast<int>(tuning::kHardMaxReplyNodes);
        testReplyLimit(limit + 1, limit);
        testReplyLimit(limit, limit);
        testReplyLimit(limit - 1, limit - 1);

        testOversizeRejection();
        testAtLimitNotRejected();
        testMalformedLedgerHashRejection(uint256::size() - 1);
        testMalformedLedgerHashRejection(uint256::size() + 1);
        testMalformedLedgerHashRejection(0);
        testWellFormedLedgerHashAccepted();

        testHitMissSplit(5, 3);
        testHitMissSplit(0, 4);
        testHitMissSplit(4, 0);

        testComputeFeeExactValues();
        testChargeUsesRequestedCount();
    }
};

BEAST_DEFINE_TESTSUITE(TMGetObjectByHash, overlay, xrpl);

}  // namespace xrpl::test
