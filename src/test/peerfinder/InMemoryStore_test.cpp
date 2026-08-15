/**
 * @file
 * @brief Tests for InMemoryStore - peer-finder in-memory store.
 */

#include <test/beast/IPEndpointCommon.h>

#include <xrpld/peerfinder/detail/InMemoryStore.h>

#include <xrpl/beast/unit_test/suite.h>

#include <cstddef>
#include <set>
#include <vector>

namespace xrpl::peer_finder::test {

/**
 * Test InMemoryStore load/save semantics.
 *
 * This is the peer-finder's in-memory store that replaces SQLite for RWDB mode.
 */
class InMemoryStore_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testBasicSaveAndLoad();
        testLoadEmptyStore();
        testOverwriteSave();
        testMultipleLoads();
    }

    void
    testBasicSaveAndLoad()
    {
        testcase("basic save and load");

        InMemoryStore store;

        Store::Entry e1;
        e1.endpoint = beast::ip::randomEP(true);
        e1.valence = 100;

        Store::Entry e2;
        e2.endpoint = beast::ip::randomEP(true);
        e2.valence = 200;

        Store::Entry e3;
        e3.endpoint = beast::ip::randomEP(true);
        e3.valence = 150;

        std::vector<Store::Entry> inputs = {e1, e2, e3};

        store.save(inputs);

        std::vector<Store::Entry> loaded;
        std::size_t const count = store.load([&loaded](beast::ip::Endpoint endpoint, int valence) {
            Store::Entry e;
            e.endpoint = endpoint;
            e.valence = valence;
            loaded.push_back(e);
        });

        BEAST_EXPECT(count == inputs.size());
        BEAST_EXPECT(loaded.size() == inputs.size());

        for (std::size_t i = 0; i < inputs.size(); ++i)
        {
            BEAST_EXPECT(loaded[i].endpoint == inputs[i].endpoint);
            BEAST_EXPECT(loaded[i].valence == inputs[i].valence);
        }
    }

    void
    testLoadEmptyStore()
    {
        testcase("load empty store");

        InMemoryStore store;

        std::size_t callCount = 0;
        std::size_t const count =
            store.load([&callCount](beast::ip::Endpoint, int) { ++callCount; });

        BEAST_EXPECT(count == 0);
        BEAST_EXPECT(callCount == 0);
    }

    void
    testOverwriteSave()
    {
        testcase("overwrite save");

        InMemoryStore store;

        Store::Entry e1;
        e1.endpoint = beast::ip::randomEP(true);
        e1.valence = 100;

        std::vector<Store::Entry> const first = {e1};

        store.save(first);

        Store::Entry e2;
        e2.endpoint = beast::ip::randomEP(true);
        e2.valence = 200;

        Store::Entry e3;
        e3.endpoint = beast::ip::randomEP(true);
        e3.valence = 300;

        std::vector<Store::Entry> second = {e2, e3};

        store.save(second);

        std::vector<Store::Entry> loaded;
        std::size_t const count = store.load([&loaded](beast::ip::Endpoint endpoint, int valence) {
            Store::Entry e;
            e.endpoint = endpoint;
            e.valence = valence;
            loaded.push_back(e);
        });

        // Should only contain the second save
        BEAST_EXPECT(count == second.size());
        BEAST_EXPECT(loaded.size() == second.size());

        for (std::size_t i = 0; i < second.size(); ++i)
        {
            BEAST_EXPECT(loaded[i].endpoint == second[i].endpoint);
            BEAST_EXPECT(loaded[i].valence == second[i].valence);
        }
    }

    void
    testMultipleLoads()
    {
        testcase("multiple loads");

        InMemoryStore store;

        beast::ip::Endpoint const ep1 = beast::ip::randomEP(true);
        beast::ip::Endpoint const ep2 = beast::ip::randomEP(true);

        Store::Entry e1;
        e1.endpoint = ep1;
        e1.valence = 100;

        Store::Entry e2;
        e2.endpoint = ep2;
        e2.valence = 200;

        std::vector<Store::Entry> const inputs = {e1, e2};

        store.save(inputs);

        // Load multiple times - should return same data each time
        for (int iter = 0; iter < 3; ++iter)
        {
            std::set<int> valences;
            std::size_t const count = store.load(
                [&valences](beast::ip::Endpoint, int valence) { valences.insert(valence); });

            BEAST_EXPECT(count == inputs.size());
            BEAST_EXPECT(valences.count(100) == 1);
            BEAST_EXPECT(valences.count(200) == 1);
        }
    }
};

BEAST_DEFINE_TESTSUITE(InMemoryStore, peerfinder, xrpl);

}  // namespace xrpl::peer_finder::test
