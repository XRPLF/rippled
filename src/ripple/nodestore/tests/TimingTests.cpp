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

namespace ripple {
namespace NodeStore {

class NodeStoreTiming_test : public TestBase
{
public:
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

            return NodeObject::createObject(NodeObjectType(type_(r_)), ledger_(r_),
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
        std::mt19937_64 r_;
        std::uniform_int_distribution<std::uint64_t> key_;
        std::uniform_int_distribution<std::uint64_t> value_;
        std::uniform_int_distribution<std::uint8_t> type_;
        std::uniform_int_distribution<std::uint32_t> ledger_;
        std::uniform_int_distribution<std::uint64_t> filler_;
    };  // end NodeFactory

    // Checks NodeFactory
    void testNodeFactory(std::int64_t const seedValue)
    {
        testcase("repeatableObject");

        NodeFactory factory(seedValue, 10000, 0, 99);

        std::set<NodeObject::Ptr, NodeObject::LessThan> out;

        for (;auto node = factory.next();){
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
        Stopwatch ()
        {
        }

        void start ()
        {
            m_startTime = beast::Time::getHighResolutionTicks ();
        }

        double getElapsed ()
        {
            std::int64_t const now = beast::Time::getHighResolutionTicks();

            return beast::Time::highResolutionTicksToSeconds (now - m_startTime);
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
    using result_type = std::map<std::string, double>;

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
        for (; auto expected = factory.next();)
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
        for (; auto node = factory.next();) backend->store(node);
    }

    static void testBatchInsert(backend_ptr& backend, NodeFactory& factory)
    {
        factory.reset();
        Batch batch;
        for (; factory.fillBatch(batch, batchSize);) backend->storeBatch(batch);
    }

    result_type benchmarkBackend(std::string const& config,
                                 std::int64_t const seedValue)
    {
        Stopwatch t;
        result_type results;

        auto params = parseDelimitedKeyValueString(config, ',');

        std::int64_t numObjects = params["num_objects"].getIntValue();
        params.remove("num_objects");

        auto manager = make_Manager();

        beast::File const path(beast::File::createTempFile("node_db"));
        params.set("path", path.getFullPathName());
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
        results["Inserts"] = t.getElapsed();

        t.start();
        testBatchInsert(backend, batchFactory);
        results["Batch Insert"] = t.getElapsed();

        t.start();
        testFetch(backend, mixedFactory, checkOkOrNotFound);
        results["Fetch 50/50"] = t.getElapsed();

        t.start();
        testFetch(backend, insertFactory, checkOk);
        results["Ordered Fetch"] = t.getElapsed();

        t.start();
        testFetch(backend, randomFactory, checkOkOrNotFound);
        results["Fetch Random"] = t.getElapsed();

        t.start();
        testFetch(backend, missingFactory, checkNotFound);
        results["Fetch Missing"] = t.getElapsed();

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
        // defaultArguments serves as an example.

        std::string defaultArguments =
            "type=rocksdb,open_files=2000,filter_bits=12,cache_mb=256,file_size_mb=8,file_size_mult=2,num_objects=100000;"
            "type=hyperleveldb,num_objects=100000";

        auto args = arg();

        if (args.empty()) args = defaultArguments;

        std::vector<std::string> configs;
        boost::split (configs, args, boost::algorithm::is_any_of (";"));

        std::map<std::string, result_type> results;

        for (auto& config : configs)
        {
            // Trim trailing comma if exists
            boost::trim_right_if(config, boost::algorithm::is_any_of(","));

            // Defaults
            if (config.find("type=") == std::string::npos)
                config += ",type=rocksdb";
            if (config.find("num_objects") == std::string::npos)
                config += ",num_objects=100000";
            results[config] = benchmarkBackend(config, seedValue);
        }

        std::stringstream ss;
        ss << std::setprecision(2) << std::fixed;
        for (auto const& header : results.begin()->second)
            ss << std::setw(14) << header.first << " ";
        ss << std::endl;
        for (auto const& result : results)
        {
            for (auto const item : result.second)
                ss << std::setw(14) << item.second << " ";
            ss << result.first << std::endl;
        }
        log << ss.str();
    }
};
BEAST_DEFINE_TESTSUITE_MANUAL(NodeStoreTiming,bench,ripple);

}  // namespace Nodestore
}