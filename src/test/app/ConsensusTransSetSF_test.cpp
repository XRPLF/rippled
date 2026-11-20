#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>

#include <xrpld/app/ledger/ConsensusTransSetSF.h>
#include <xrpld/app/ledger/TransactionMaster.h>
#include <xrpld/app/misc/Transaction.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/SHAMapHash.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapMissingNode.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace xrpl::test {

/**
 * ConsensusTransSetSF must answer only for data that hashes to the key it was
 * asked for. Its callers adopt that hash rather than recomputing one, and
 * `canonicalize()` publishes the node to the tree node cache every SHAMap of
 * the family reads.
 */
struct ConsensusTransSetSF_test : public beast::unit_test::Suite
{
    /**
     * A node in the hash-prefixed form a filter serves. The hash returned is
     * the one the parse computed from the contents, so a case checks the filter
     * against the parse rather than against itself.
     *
     * @return The prefixed serialization and the node's own hash.
     */
    [[nodiscard]] static std::pair<Blob, SHAMapHash>
    prefixedNode()
    {
        static constexpr auto kWordsPerTx = 4u;

        Serializer wire;
        for (auto word = 0u; word < kWordsPerTx; ++word)
            wire.add32(0xDEADBEEFu + word);
        wire.add8(kWireTypeTransaction);

        auto const node = SHAMapTreeNode::makeFromWire(makeSlice(wire.peekData()));
        if (!node)
            return {};

        Serializer prefixed;
        node->serializeWithPrefix(prefixed);
        return {prefixed.getData(), node->getHash()};
    }

    /**
     * A cache of this filter's own, empty and independent of the application's.
     *
     * @param j Where the cache logs.
     * @return The cache.
     */
    [[nodiscard]] static ConsensusTransSetSF::NodeCache
    ownCache(beast::Journal j)
    {
        static constexpr auto kEntries = 64;
        static constexpr auto kExpiry = std::chrono::seconds{300};
        return ConsensusTransSetSF::NodeCache{"test", kEntries, kExpiry, stopwatch(), j};
    }

    /**
     * A blob filed under a key it does not produce is refused.
     *
     * @param env The environment to run in.
     */
    void
    testMisfiledCachedNodeIsRefused(jtx::Env& env)
    {
        testcase("A cached node that does not hash to its key is refused");

        auto const [blob, realHash] = prefixedNode();
        BEAST_EXPECT(!blob.empty());
        if (blob.empty())
            return;

        // Any key the blob does not produce. The cache is filed under it anyway.
        SHAMapHash const wrongHash{UInt256{7}};
        BEAST_EXPECT(wrongHash != realHash);

        auto cache = ownCache(env.app().getJournal("test"));
        cache.insert(wrongHash, blob);

        ConsensusTransSetSF const sf(env.app(), cache);
        auto& family = env.app().getNodeFamily();

        SHAMap map{SHAMapType::TRANSACTION, family};
        BEAST_EXPECT(!map.fetchRoot(wrongHash, &sf));

        // Every SHAMap of this family reads that cache by bare hash, so the node stays out of it.
        BEAST_EXPECT(!family.getTreeNodeCache()->fetch(wrongHash.asUInt256()));

        // The entry is dropped, which lets the refusal fall through to the transaction arm.
        Blob stale;
        BEAST_EXPECT(!cache.retrieve(wrongHash, stale));
    }

    /**
     * The same blob under its own key is served, so the refusal above comes
     * from the comparison rather than from the harness.
     *
     * @param env The environment to run in.
     */
    void
    testCorrectlyFiledCachedNodeIsServed(jtx::Env& env)
    {
        testcase("A cached node that hashes to its key is served");

        auto const [blob, realHash] = prefixedNode();
        BEAST_EXPECT(!blob.empty());
        if (blob.empty())
            return;

        auto cache = ownCache(env.app().getJournal("test"));
        cache.insert(realHash, blob);

        ConsensusTransSetSF const sf(env.app(), cache);
        auto& family = env.app().getNodeFamily();

        SHAMap map{SHAMapType::TRANSACTION, family};
        BEAST_EXPECT(map.fetchRoot(realHash, &sf));
        BEAST_EXPECT(map.getHash() == realHash);

        // An accepted node is published, which gives the empty cache above its meaning.
        BEAST_EXPECT(family.getTreeNodeCache()->fetch(realHash.asUInt256()) != nullptr);
    }

    /**
     * The cache arm answers first, and an unknown key answers nothing.
     *
     * The transaction arm reads a different store, which holds nothing for the
     * keys used here, so this case reaches the cache arm alone.
     *
     * @param env The environment to run in.
     */
    void
    testCacheArmAnswersFirst(jtx::Env& env)
    {
        testcase("The cache arm answers first, and an unknown key answers nothing");

        auto const [blob, realHash] = prefixedNode();
        BEAST_EXPECT(!blob.empty());
        if (blob.empty())
            return;

        // Filed in the cache, so a hit shows that arm is consulted before the transaction arm.
        auto cache = ownCache(env.app().getJournal("test"));
        cache.insert(realHash, blob);

        ConsensusTransSetSF const sf(env.app(), cache);
        BEAST_EXPECT(sf.getNode(realHash).has_value());

        // A key neither arm holds answers nothing.
        BEAST_EXPECT(!sf.getNode(SHAMapHash{UInt256{11}}).has_value());
    }

    /**
     * A transaction filed under a key it does not serialize to is refused.
     *
     * The master transaction store is keyed by a transaction's own id, and
     * `getCache()` hands that store out by reference, so a caller can file
     * under any key. `TransactionMaster::fetch()` reaches the same state in
     * production.
     *
     * @param env The environment to run in.
     */
    void
    testMisfiledTransactionIsRefused(jtx::Env& env)
    {
        testcase("A cached transaction that does not serialize to its key is refused");

        jtx::Account const alice{"alice"};
        jtx::Account const bob{"bob"};
        env.fund(jtx::XRP(10000), alice, bob);
        env.close();

        // A real transaction, so its id is the digest of exactly the bytes getNode() serializes.
        auto const jt = env.jt(jtx::pay(alice, bob, jtx::XRP(1)));
        BEAST_EXPECT(jt.stx != nullptr);
        if (!jt.stx)
            return;

        std::string reason;
        auto txn = std::make_shared<Transaction>(jt.stx, reason, env.app());
        BEAST_EXPECT(txn->getID() == jt.stx->getTransactionID());

        // Any key the transaction does not produce.
        UInt256 const wrongKey{13};
        BEAST_EXPECT(wrongKey != jt.stx->getTransactionID());

        auto& txCache = env.app().getMasterTransaction().getCache();
        static_cast<void>(txCache.canonicalizeReplaceClient(wrongKey, txn));

        // The state the refusal is about, read back through the same call getNode() makes.
        BEAST_EXPECT(env.app().getMasterTransaction().fetchFromCache(wrongKey) != nullptr);

        // This filter's own cache is empty, so the transaction arm is the arm that answers.
        auto cache = ownCache(env.app().getJournal("test"));
        ConsensusTransSetSF const sf(env.app(), cache);
        BEAST_EXPECT(!sf.getNode(SHAMapHash{wrongKey}).has_value());

        // Control: the same transaction under its own id is served, so the refusal above comes
        // from the comparison rather than from never reaching the arm.
        auto const& realKey = jt.stx->getTransactionID();
        static_cast<void>(txCache.canonicalizeReplaceClient(realKey, txn));
        BEAST_EXPECT(sf.getNode(SHAMapHash{realKey}).has_value());
    }

    void
    run() override
    {
        jtx::Env env{*this};

        testMisfiledCachedNodeIsRefused(env);
        testCorrectlyFiledCachedNodeIsServed(env);
        testCacheArmAnswersFirst(env);
        testMisfiledTransactionIsRefused(env);
    }
};

BEAST_DEFINE_TESTSUITE(ConsensusTransSetSF, app, xrpl);

}  // namespace xrpl::test
