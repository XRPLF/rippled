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

#include <test/nodestore/TestBase.h>
#include <ripple/nodestore/DummyScheduler.h>
#include <ripple/nodestore/Manager.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/unity/rocksdb.h>
#include <ripple/beast/utility/temp_dir.h>
#include <ripple/beast/xor_shift_engine.h>
#include <ripple/beast/unit_test.h>
#include <test/unit_test/SuiteJournal.h>
#include <beast/unit_test/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <atomic>
#include <chrono>
#include <iterator>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#ifndef NODESTORE_TIMING_DO_VERIFY
#define NODESTORE_TIMING_DO_VERIFY 0
#endif

namespace ripple {
namespace NodeStore {

// Fill memory with random bits
template <class Generator>
static
void
rngcpy (void* buffer, std::size_t bytes, Generator& g)
{
    using result_type = typename Generator::result_type;
    while (bytes >= sizeof(result_type))
    {
        auto const v = g();
        memcpy(buffer, &v, sizeof(v));
        buffer = reinterpret_cast<std::uint8_t*>(buffer) + sizeof(v);
        bytes -= sizeof(v);
    }

    if (bytes > 0)
    {
        auto const v = g();
        memcpy(buffer, &v, bytes);
    }
}

// Instance of node factory produces a deterministic sequence
// of random NodeObjects within the given
class Sequence
{
private:
    enum
    {
        minLedger = 1,
        maxLedger = 1000000,
        minSize = 250,
        maxSize = 1250
    };

    beast::xor_shift_engine gen_;
    std::uint8_t prefix_;
    std::discrete_distribution<std::uint32_t> d_type_;
    std::uniform_int_distribution<std::uint32_t> d_size_;

public:
    explicit
    Sequence(std::uint8_t prefix)
        : prefix_ (prefix)
        // uniform distribution over hotLEDGER - hotTRANSACTION_NODE
        // but exclude  hotTRANSACTION = 2 (removed)
        , d_type_ ({1, 1, 0, 1, 1})
        , d_size_ (minSize, maxSize)
    {
    }

    // Returns the n-th key
    uint256
    key (std::size_t n)
    {
        gen_.seed(n+1);
        uint256 result;
        rngcpy (&*result.begin(), result.size(), gen_);
        return result;
    }

    // Returns the n-th complete NodeObject
    std::shared_ptr<NodeObject>
    obj (std::size_t n)
    {
        gen_.seed(n+1);
        uint256 key;
        auto const data =
            static_cast<std::uint8_t*>(&*key.begin());
        *data = prefix_;
        rngcpy (data + 1, key.size() - 1, gen_);
        Blob value(d_size_(gen_));
        rngcpy (&value[0], value.size(), gen_);
        return NodeObject::createObject (
            safe_cast<NodeObjectType>(d_type_(gen_)),
                std::move(value), key);
    }

    // returns a batch of NodeObjects starting at n
    void
    batch (std::size_t n, Batch& b, std::size_t size)
    {
        b.clear();
        b.reserve (size);
        while(size--)
            b.emplace_back(obj(n++));
    }
};

//----------------------------------------------------------------------------------

class Timing_test : public beast::unit_test::suite
{
public:
    enum
    {
        // percent of fetches for missing nodes
        missingNodePercent = 20
    };

    std::size_t const default_repeat = 3;
#ifndef NDEBUG
    std::size_t const default_items = 10000;
#else
    std::size_t const default_items = 100000; // release
#endif

    using clock_type = std::chrono::steady_clock;
    using duration_type = std::chrono::milliseconds;

    struct Params
    {
        std::size_t items;
        std::size_t threads;
    };

    static
    std::string
    to_string (Section const& config)
    {
        std::string s;
        for (auto iter = config.begin(); iter != config.end(); ++iter)
            s += (iter != config.begin() ? "," : "") +
                iter->first + "=" + iter->second;
        return s;
    }

    static
    std::string
    to_string (duration_type const& d)
    {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(3) <<
            (d.count() / 1000.) << "s";
        return ss.str();
    }

    static
    Section
    parse (std::string s)
    {
        Section section;
        std::vector <std::string> v;
        boost::split (v, s,
            boost::algorithm::is_any_of (","));
        section.append(v);
        return section;
    }

    //--------------------------------------------------------------------------

    // Workaround for GCC's parameter pack expansion in lambdas
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=47226
    template <class Body>
    class parallel_for_lambda
    {
    private:
        std::size_t const n_;
        std::atomic<std::size_t>& c_;

    public:
        parallel_for_lambda (std::size_t n,
                std::atomic<std::size_t>& c)
            : n_ (n)
            , c_ (c)
        {
        }

        template <class... Args>
        void
        operator()(Args&&... args)
        {
            Body body(args...);
            for(;;)
            {
                auto const i = c_++;
                if (i >= n_)
                    break;
                body (i);
            }
        }
    };

    /*  Execute parallel-for loop.

        Constructs `number_of_threads` instances of `Body`
        with `args...` parameters and runs them on individual threads
        with unique loop indexes in the range [0, n).
    */
    template <class Body, class... Args>
    void
    parallel_for (std::size_t const n,
        std::size_t number_of_threads, Args const&... args)
    {
        std::atomic<std::size_t> c(0);
        std::vector<beast::unit_test::thread> t;
        t.reserve(number_of_threads);
        for (std::size_t id = 0; id < number_of_threads; ++id)
            t.emplace_back(*this,
                parallel_for_lambda<Body>(n, c),
                    args...);
        for (auto& _ : t)
            _.join();
    }

    template <class Body, class... Args>
    void
    parallel_for_id (std::size_t const n,
        std::size_t number_of_threads, Args const&... args)
    {
        std::atomic<std::size_t> c(0);
        std::vector<beast::unit_test::thread> t;
        t.reserve(number_of_threads);
        for (std::size_t id = 0; id < number_of_threads; ++id)
            t.emplace_back(*this,
                parallel_for_lambda<Body>(n, c),
                    id, args...);
        for (auto& _ : t)
            _.join();
    }

    //--------------------------------------------------------------------------

    // Insert only
    void
    do_insert (Section const& config,
        Params const& params, beast::Journal journal)
    {
        DummyScheduler scheduler;
        auto backend = make_Backend (config, scheduler, journal);
        BEAST_EXPECT(backend != nullptr);
        backend->open();

        class Body
        {
        private:
            suite& suite_;
            Backend& backend_;
            Sequence seq_;

        public:
            explicit
            Body (suite& s, Backend& backend)
                : suite_ (s)
                , backend_ (backend)
                , seq_(1)
            {
            }

            void
            operator()(std::size_t i)
            {
                try
                {
                    backend_.store(seq_.obj(i));
                }
                catch(std::exception const& e)
                {
                    suite_.fail(e.what());
                }
            }
        };

        try
        {
            parallel_for<Body>(params.items,
                params.threads, std::ref(*this), std::ref(*backend));
        }
        catch (std::exception const&)
        {
        #if NODESTORE_TIMING_DO_VERIFY
            backend->verify();
        #endif
            Rethrow();
        }
        backend->close();
    }

    // Fetch existing keys
    void
    do_fetch (Section const& config,
        Params const& params, beast::Journal journal)
    {
        DummyScheduler scheduler;
        auto backend = make_Backend (config, scheduler, journal);
        BEAST_EXPECT(backend != nullptr);
        backend->open();

        class Body
        {
        private:
            suite& suite_;
            Backend& backend_;
            Sequence seq1_;
            beast::xor_shift_engine gen_;
            std::uniform_int_distribution<std::size_t> dist_;

        public:
            Body (std::size_t id, suite& s,
                    Params const& params, Backend& backend)
                : suite_(s)
                , backend_ (backend)
                , seq1_ (1)
                , gen_ (id + 1)
                , dist_ (0, params.items - 1)
            {
            }

            void
            operator()(std::size_t i)
            {
                try
                {
                    std::shared_ptr<NodeObject> obj;
                    std::shared_ptr<NodeObject> result;
                    obj = seq1_.obj(dist_(gen_));
                    backend_.fetch(obj->getHash().data(), &result);
                    suite_.expect(result && isSame(result, obj));
                }
                catch(std::exception const& e)
                {
                    suite_.fail(e.what());
                }
            }
        };
        try
        {
            parallel_for_id<Body>(params.items, params.threads,
                std::ref(*this), std::ref(params), std::ref(*backend));
        }
        catch (std::exception const&)
        {
        #if NODESTORE_TIMING_DO_VERIFY
            backend->verify();
        #endif
            Rethrow();
        }
        backend->close();
    }

    // Perform lookups of non-existent keys
    void
    do_missing (Section const& config,
        Params const& params, beast::Journal journal)
    {
        DummyScheduler scheduler;
        auto backend = make_Backend (config, scheduler, journal);
        BEAST_EXPECT(backend != nullptr);
        backend->open();

        class Body
        {
        private:
            suite& suite_;
            //Params const& params_;
            Backend& backend_;
            Sequence seq2_;
            beast::xor_shift_engine gen_;
            std::uniform_int_distribution<std::size_t> dist_;

        public:
            Body (std::size_t id, suite& s,
                    Params const& params, Backend& backend)
                : suite_ (s)
                //, params_ (params)
                , backend_ (backend)
                , seq2_ (2)
                , gen_ (id + 1)
                , dist_ (0, params.items - 1)
            {
            }

            void
            operator()(std::size_t i)
            {
                try
                {
                    auto const key = seq2_.key(i);
                    std::shared_ptr<NodeObject> result;
                    backend_.fetch(key.data(), &result);
                    suite_.expect(! result);
                }
                catch(std::exception const& e)
                {
                    suite_.fail(e.what());
                }
            }
        };

        try
        {
            parallel_for_id<Body>(params.items, params.threads,
                std::ref(*this), std::ref(params), std::ref(*backend));
        }
        catch (std::exception const&)
        {
        #if NODESTORE_TIMING_DO_VERIFY
            backend->verify();
        #endif
            Rethrow();
        }
        backend->close();
    }

    // Fetch with present and missing keys
    void
    do_mixed (Section const& config,
        Params const& params, beast::Journal journal)
    {
        DummyScheduler scheduler;
        auto backend = make_Backend (config, scheduler, journal);
        BEAST_EXPECT(backend != nullptr);
        backend->open();

        class Body
        {
        private:
            suite& suite_;
            //Params const& params_;
            Backend& backend_;
            Sequence seq1_;
            Sequence seq2_;
            beast::xor_shift_engine gen_;
            std::uniform_int_distribution<std::uint32_t> rand_;
            std::uniform_int_distribution<std::size_t> dist_;

        public:
            Body (std::size_t id, suite& s,
                    Params const& params, Backend& backend)
                : suite_ (s)
                //, params_ (params)
                , backend_ (backend)
                , seq1_ (1)
                , seq2_ (2)
                , gen_ (id + 1)
                , rand_ (0, 99)
                , dist_ (0, params.items - 1)
            {
            }

            void
            operator()(std::size_t i)
            {
                try
                {
                    if (rand_(gen_) < missingNodePercent)
                    {
                        auto const key = seq2_.key(dist_(gen_));
                        std::shared_ptr<NodeObject> result;
                        backend_.fetch(key.data(), &result);
                        suite_.expect(! result);
                    }
                    else
                    {
                        std::shared_ptr<NodeObject> obj;
                        std::shared_ptr<NodeObject> result;
                        obj = seq1_.obj(dist_(gen_));
                        backend_.fetch(obj->getHash().data(), &result);
                        suite_.expect(result && isSame(result, obj));
                    }
                }
                catch(std::exception const& e)
                {
                    suite_.fail(e.what());
                }
            }
        };

        try
        {
            parallel_for_id<Body>(params.items, params.threads,
                std::ref(*this), std::ref(params), std::ref(*backend));
        }
        catch (std::exception const&)
        {
        #if NODESTORE_TIMING_DO_VERIFY
            backend->verify();
        #endif
            Rethrow();
        }
        backend->close();
    }

    // Simulate a rippled workload:
    // Each thread randomly:
    //      inserts a new key
    //      fetches an old key
    //      fetches recent, possibly non existent data
    void
    do_work (Section const& config,
        Params const& params, beast::Journal journal)
    {
        DummyScheduler scheduler;
        auto backend = make_Backend (config, scheduler, journal);
        BEAST_EXPECT(backend != nullptr);
        backend->setDeletePath();
        backend->open();

        class Body
        {
        private:
            suite& suite_;
            Params const& params_;
            Backend& backend_;
            Sequence seq1_;
            beast::xor_shift_engine gen_;
            std::uniform_int_distribution<std::uint32_t> rand_;
            std::uniform_int_distribution<std::size_t> recent_;
            std::uniform_int_distribution<std::size_t> older_;

        public:
            Body (std::size_t id, suite& s,
                    Params const& params, Backend& backend)
                : suite_ (s)
                , params_ (params)
                , backend_ (backend)
                , seq1_ (1)
                , gen_ (id + 1)
                , rand_ (0, 99)
                , recent_ (params.items, params.items * 2 - 1)
                , older_ (0, params.items - 1)
            {
            }

            void
            operator()(std::size_t i)
            {
                try
                {
                    if (rand_(gen_) < 200)
                    {
                        // historical lookup
                        std::shared_ptr<NodeObject> obj;
                        std::shared_ptr<NodeObject> result;
                        auto const j = older_(gen_);
                        obj = seq1_.obj(j);
                        std::shared_ptr<NodeObject> result1;
                        backend_.fetch(obj->getHash().data(), &result);
                        suite_.expect(result != nullptr);
                        suite_.expect(isSame(result, obj));
                    }

                    char p[2];
                    p[0] = rand_(gen_) < 50 ? 0 : 1;
                    p[1] = 1 - p[0];
                    for (int q = 0; q < 2; ++q)
                    {
                        switch (p[q])
                        {
                        case 0:
                        {
                            // fetch recent
                            std::shared_ptr<NodeObject> obj;
                            std::shared_ptr<NodeObject> result;
                            auto const j = recent_(gen_);
                            obj = seq1_.obj(j);
                            backend_.fetch(obj->getHash().data(), &result);
                            suite_.expect(! result ||
                                isSame(result, obj));
                            break;
                        }

                        case 1:
                        {
                            // insert new
                            auto const j = i + params_.items;
                            backend_.store(seq1_.obj(j));
                            break;
                        }
                        }
                    }
                }
                catch(std::exception const& e)
                {
                    suite_.fail(e.what());
                }
            }
        };

        try
        {
            parallel_for_id<Body>(params.items, params.threads,
                std::ref(*this), std::ref(params), std::ref(*backend));
        }
        catch (std::exception const&)
        {
        #if NODESTORE_TIMING_DO_VERIFY
            backend->verify();
        #endif
            Rethrow();
        }
        backend->close();
    }

    //--------------------------------------------------------------------------

    using test_func = void (Timing_test::*)(
        Section const&, Params const&, beast::Journal);
    using test_list = std::vector <std::pair<std::string, test_func>>;

    duration_type
    do_test (test_func f,
        Section const& config, Params const& params, beast::Journal journal)
    {
        auto const start = clock_type::now();
        (this->*f)(config, params, journal);
        return std::chrono::duration_cast<duration_type> (
            clock_type::now() - start);
    }

    void
    do_tests (std::size_t threads, test_list const& tests,
        std::vector<std::string> const& config_strings)
    {
        using std::setw;
        int w = 8;
        for (auto const& test : tests)
            if (w < test.first.size())
                w = test.first.size();
        log <<
            threads << " Thread" << (threads > 1 ? "s" : "") << ", " <<
            default_items << " Objects" << std::endl;
        {
            std::stringstream ss;
            ss << std::left << setw(10) << "Backend" << std::right;
            for (auto const& test : tests)
                ss << " " << setw(w) << test.first;
            log << ss.str() << std::endl;
        }

        using namespace beast::severities;
        test::SuiteJournal journal ("Timing_test", *this);

        for (auto const& config_string : config_strings)
        {
            Params params;
            params.items = default_items;
            params.threads = threads;
            for (auto i = default_repeat; i--;)
            {
                beast::temp_dir tempDir;
                Section config = parse(config_string);
                config.set ("path", tempDir.path());
                std::stringstream ss;
                ss << std::left << setw(10) <<
                    get(config, "type", std::string()) << std::right;
                for (auto const& test : tests)
                    ss << " " << setw(w) << to_string(
                        do_test (test.second, config, params, journal));
                ss << "   " << to_string(config);
                log << ss.str() << std::endl;
            }
        }
    }

    void
    run() override
    {
        testcase ("Timing", beast::unit_test::abort_on_fail);

        /*  Parameters:

            repeat          Number of times to repeat each test
            items           Number of objects to create in the database

        */
        std::string default_args =
            "type=nudb"
        #if RIPPLE_ROCKSDB_AVAILABLE
            ";type=rocksdb,open_files=2000,filter_bits=12,cache_mb=256,"
                "file_size_mb=8,file_size_mult=2"
        #endif
        #if 0
            ";type=memory|path=NodeStore"
        #endif
            ;

        test_list const tests =
            {
                 { "Insert",    &Timing_test::do_insert }
                ,{ "Fetch",     &Timing_test::do_fetch }
                ,{ "Missing",   &Timing_test::do_missing }
                ,{ "Mixed",     &Timing_test::do_mixed }
                ,{ "Work",      &Timing_test::do_work }
            };

        auto args = arg().empty() ? default_args : arg();
        std::vector <std::string> config_strings;
        boost::split (config_strings, args,
            boost::algorithm::is_any_of (";"));
        for (auto iter = config_strings.begin();
                iter != config_strings.end();)
            if (iter->empty())
                iter = config_strings.erase (iter);
            else
                ++iter;

        do_tests ( 1, tests, config_strings);
        do_tests ( 4, tests, config_strings);
        do_tests ( 8, tests, config_strings);
        //do_tests (16, tests, config_strings);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(Timing,NodeStore,ripple,1);

}
}

