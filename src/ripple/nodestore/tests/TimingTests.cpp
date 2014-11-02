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

#include <limits>

namespace ripple {
namespace NodeStore {

class NodeStoreTiming_test : public TestBase
{
public:
    // Simple and fast RNG based on:
    // http://xorshift.di.unimi.it/xorshift128plus.c
    class XORShiftEngine{
       public:
        using result_type = std::uint64_t;
        static const result_type default_seed = 1977u;

        explicit XORShiftEngine(result_type val = default_seed) {}

        void seed(result_type const seed)
        {
            s[0] = murmuhash3(seed);
            s[1] = murmuhash3(s[0]);
        }

        result_type operator()()
        {
            result_type s1 = s[0];
            const result_type s0 = s[1];
            s[0] = s0;
            s1 ^= s1 << 23;
            return (s[1] = (s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26))) + s0;
        }

        static constexpr result_type min()
        {
            return std::numeric_limits<result_type>::min();
        }

        static constexpr result_type max()
        {
            return std::numeric_limits<result_type>::max();
        }

       private:
        result_type s[2];

        static result_type murmuhash3(result_type x)
        {
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdULL;
            x ^= x >> 33;
            x *= 0xc4ceb9fe1a85ec53ULL;
            return x ^= x >> 33;
        }
    };

    class NodeFactory
    {
        enum
        {
            minLedger = 1,
            maxLedger = 10000000,
            minValueLength = 128,  // Must be a multiple of 8
            maxValueLength = 256   // Will be multiplied by 8
        };

        public : NodeFactory(std::int64_t seed,
                                         std::int64_t numObjects,
                                         std::int64_t minKey,
                                         std::int64_t maxKey)
            : seed_(seed),
              numObjects_(numObjects),
              count_(0),
              rng_(seed),
              key_(minKey, maxKey),
              value_(minValueLength, maxValueLength),
              type_(hotLEDGER, hotTRANSACTION_NODE),
              ledger_(minLedger, maxLedger)
        {
        }

        NodeObject::Ptr next()
        {
            // Stop when done
            if (count_==numObjects_) return nullptr;
            count_++;

            // Seed from range between minKey and maxKey to ensure repeatability
            r_.seed(key_(rng_));

            uint256 hash;
            std::generate_n(reinterpret_cast<uint64_t*>(hash.begin()),
                            hash.size() / sizeof(std::uint64_t),
                            std::bind(filler_, r_));

            Blob data(value_(r_)*8);
            std::generate_n(reinterpret_cast<uint64_t*>(data.data()),
                            data.size() / sizeof(std::uint64_t),
                            std::bind(filler_, r_));

            NodeObjectType nodeType(static_cast<NodeObjectType>(type_(r_)));
            return NodeObject::createObject(nodeType, ledger_(r_),
                                            std::move(data), hash);
        }

        bool fillBatch(Batch& batch,std::int64_t batchSize)
        {
            batch.clear();
            for (std::uint64_t i = 0; i < batchSize; i++)
            {
                auto node = next();
                if (!node)
                    return false;
                batch.emplace_back(node);
            }
            return true;
        }

        void reset()
        {
            count_ = 0;
            rng_.seed(seed_);
        }

       private:
        std::int64_t seed_;
        std::int64_t numObjects_;
        std::int64_t count_;
        std::mt19937_64 rng_;
        XORShiftEngine r_;
        std::uniform_int_distribution<std::uint64_t> key_;
        std::uniform_int_distribution<std::uint64_t> value_;
        std::uniform_int_distribution<std::uint32_t> type_;
        std::uniform_int_distribution<std::uint32_t> ledger_;
        std::uniform_int_distribution<std::uint64_t> filler_;
    };  // end NodeFactory

    // Checks NodeFactory
    void testNodeFactory(std::int64_t const seedValue)
    {
        testcase("repeatableObject");

        NodeFactory factory(seedValue, 10000, 0, 99);

        std::set<NodeObject::Ptr, NodeObject::LessThan> out;

        while (auto node = factory.next())
        {
            auto it = out.find(node);
            if (it == out.end())
            {
                out.insert(node);
            }
            else
            {
                expect(it->get()->isCloneOf(node), "Should be clones");
            }
        }
        expect(out.size() == 100, "Too many objects created");
    }

    class Stopwatch
    {
    public:
     Stopwatch() {}

     void start() { m_startTime = beast::Time::getHighResolutionTicks(); }

     double getElapsed()
     {
         std::int64_t const now = beast::Time::getHighResolutionTicks();

         return beast::Time::highResolutionTicksToSeconds(now - m_startTime);
     }

    private:
        std::int64_t m_startTime;
    };

    //--------------------------------------------------------------------------

    enum
    {
        batchSize = 128
    };

    using check_func = std::function<bool(Status const)>;
    using backend_ptr = std::unique_ptr<Backend>;
    using manager_ptr = std::unique_ptr<Manager>;
    using result_type = std::vector<std::pair<std::string, double>>;

    static bool checkNotFound(Status const status)
    {
        return status == notFound;
    };

    static bool checkOk(Status const status) { return status == ok; };

    static bool checkOkOrNotFound(Status const status)
    {
        return (status == ok) || (status == notFound);
    };

    void testFetch(backend_ptr& backend, NodeFactory& factory,
                   check_func f)
    {
        factory.reset();
        while (auto expected = factory.next())
        {
            NodeObject::Ptr got;

            Status const status =
                backend->fetch(expected->getHash().cbegin(), &got);
            expect(f(status), "Wrong status");
            if (status == ok)
            {
                expect(got != nullptr, "Should not be null");
                expect(got->isCloneOf(expected), "Should be clones");
            }
        }
    }

    static void testInsert(backend_ptr& backend, NodeFactory& factory)
    {
        factory.reset();
        while (auto node = factory.next()) backend->store(node);
    }

    static void testBatchInsert(backend_ptr& backend, NodeFactory& factory)
    {
        factory.reset();
        Batch batch;
        while (factory.fillBatch(batch, batchSize)) backend->storeBatch(batch);
    }

    result_type benchmarkBackend(beast::StringPairArray const& params,
                                 std::int64_t const seedValue,
                                 std::int64_t const numObjects)
    {
        Stopwatch t;
        result_type results;

        auto manager = make_Manager();
        DummyScheduler scheduler;
        beast::Journal j;

        auto backend = manager->make_Backend(params, scheduler, j);

        NodeFactory insertFactory(seedValue, numObjects, 0, numObjects);
        NodeFactory batchFactory(seedValue, numObjects, numObjects * 10,
                                 numObjects * 11);

        // Twice the range of insert
        NodeFactory mixedFactory(seedValue, numObjects, numObjects,
                                 numObjects * 2);
        // Same as batch, different order
        NodeFactory randomFactory(seedValue + 1, numObjects, numObjects * 10,
                                  numObjects * 11);
        // Don't exist
        NodeFactory missingFactory(seedValue, numObjects, numObjects * 3,
                                   numObjects * 4);

        t.start();
        testInsert(backend, insertFactory);
        results.emplace_back("Inserts", t.getElapsed());

        t.start();
        testBatchInsert(backend, batchFactory);
        results.emplace_back("Batch Insert", t.getElapsed());

        t.start();
        testFetch(backend, mixedFactory, checkOkOrNotFound);
        results.emplace_back("Fetch 50/50", t.getElapsed());

        t.start();
        testFetch(backend, insertFactory, checkOk);
        results.emplace_back("Ordered Fetch", t.getElapsed());

        t.start();
        testFetch(backend, randomFactory, checkOkOrNotFound);
        results.emplace_back("Fetch Random", t.getElapsed());

        t.start();
        testFetch(backend, missingFactory, checkNotFound);
        results.emplace_back("Fetch Missing", t.getElapsed());

        return results;
    }

    //--------------------------------------------------------------------------

    void run ()
    {
        int const seedValue = 50;
        
        testNodeFactory(seedValue);

        // Expects a semi-colon delimited list of backend configurations.
        // Each configuration is a comma delimited list of key-value pairs.
        // Each pair is separated by a '='.
        // 'type' defaults to 'rocksdb'
        // 'num_objects' defaults to '100000'
        // 'num_runs' defaults to '3'
        // defaultArguments serves as an example.

        std::string defaultArguments =
            "type=rocksdb,open_files=2000,filter_bits=12,cache_mb=256"
            "file_size_mb=8,file_size_mult=2,num_objects=100000,num_runs=3;"
            "type=hyperleveldb,num_objects=100000,num_runs=3";

        auto args = arg();

        if (args.empty()) args = defaultArguments;

        std::vector<std::string> configs;
        boost::split (configs, args, boost::algorithm::is_any_of (";"));

        std::map<std::string, std::vector<result_type>> results;

        for (auto& config : configs)
        {
            auto params = parseDelimitedKeyValueString(config, ',');

            // Defaults
            std::int64_t numRuns = 3;
            std::int64_t numObjects = 100000;

            if (!params["num_objects"].isEmpty())
                numObjects = params["num_objects"].getIntValue();

            if (!params["num_runs"].isEmpty())
                numRuns = params["num_runs"].getIntValue();

            if (params["type"].isEmpty())
                params.set("type", "rocksdb");

            for (std::int64_t i = 0; i < numRuns; i++)
            {
                beast::UnitTestUtilities::TempDirectory path("node_db");
                params.set("path", path.getFullPathName());
                results[config].emplace_back(
                    benchmarkBackend(params, seedValue + i, numObjects));
            }
        }

        std::stringstream header;
        std::stringstream stats;
        std::stringstream legend;

        auto firstRun = results.begin()->second.begin();
        header << std::setw(7) << "Config" << std::setw(4) << "Run";
        for (auto const& title : *firstRun)
            header << std::setw(14) << title.first;

        stats << std::setprecision(2) << std::fixed;

        std::int64_t resultCount = 0;
        for (auto const& result : results)
        {
            std::int64_t runCount = 0;
            for (auto const& run : result.second)
            {
                stats << std::setw(7) << resultCount << std::setw(4)
                      << runCount;
                for (auto const& item : run)
                    stats << std::setw(14) << item.second;
                runCount++;
                stats << std::endl;

            }
            legend << std::setw(2) << resultCount << ": " << result.first
                   << std::endl;
            resultCount++;
        }
        log << header.str() << std::endl << stats.str() << std::endl
            << "Configs:" << std::endl << legend.str();
    }
};
BEAST_DEFINE_TESTSUITE_MANUAL(NodeStoreTiming,bench,ripple);

}  // namespace Nodestore
}
