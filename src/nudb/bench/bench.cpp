//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <nudb/test/test_store.hpp>
#include <nudb/util.hpp>
#include <beast/unit_test/dstream.hpp>

#if WITH_ROCKSDB
#include "rocksdb/db.h"

char const* rocksdb_build_git_sha="Benchmark Dummy Sha";
char const* rocksdb_build_compile_date="Benchmark Dummy Compile Date";
#endif

#include <boost/container/flat_map.hpp>
#include <boost/program_options.hpp>
#include <boost/system/system_error.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <thread>
#include <utility>

namespace nudb {
namespace test {

beast::unit_test::dstream dout{std::cout};
beast::unit_test::dstream derr{std::cerr};

struct stop_watch
{
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    time_point start_;

    stop_watch() : start_(clock::now())
    {
    }

    std::chrono::duration<double>
    elapsed() const
    {
        return std::chrono::duration_cast<std::chrono::duration<double>>(
            clock::now() - start_);
    }
};

class bench_progress
{
    progress p_;
    std::uint64_t const total_=0;
    std::uint64_t batch_start_=0;

public:
    bench_progress(std::ostream& os, std::uint64_t total)
        : p_(os), total_(total)
    {
        p_(0, total);
    }
    void
    update(std::uint64_t batch_amount)
    {
        p_(batch_start_ + batch_amount, total_);
        batch_start_ += batch_amount;
    }
};

class gen_key_value
{
    test_store& ts_;
    std::uint64_t cur_;

public:
    gen_key_value(test_store& ts, std::uint64_t cur)
        : ts_(ts),
          cur_(cur)
    {
    }
    item_type
    operator()()
    {
        return ts_[cur_++];
    }
};

class rand_existing_key
{
    xor_shift_engine rng_;
    std::uniform_int_distribution<std::uint64_t> dist_;
    test_store& ts_;

  public:
      rand_existing_key(test_store& ts,
          std::uint64_t max_index,
          std::uint64_t seed = 1337)
          : dist_(0, max_index),
            ts_(ts)
      {
          rng_.seed(seed);
    }
    item_type
    operator()()
    {
        return ts_[dist_(rng_)];
    }
};


template <class Generator, class F>
std::chrono::duration<double>
time_block(std::uint64_t n, Generator&& g, F&& f)
{
    stop_watch timer;
    for (std::uint64_t i = 0; i < n; ++i)
    {
        f(g());
    }
    return timer.elapsed();
}

template <class Inserter, class Fetcher, class AddSample, class PreFetchHook>
void
time_fetch_insert_interleaved(
    std::uint64_t batch_size,
    std::uint64_t num_batches,
    test_store& ts,
    Inserter&& inserter,
    Fetcher&& fetcher,
    AddSample&& add_sample,
    PreFetchHook&& pre_fetch_hook,
    bench_progress& progress)
{
    std::uint64_t next_insert_index = 0;
    for (auto b = 0ull; b < num_batches; ++b)
    {
        auto const insert_time = time_block(
            batch_size, gen_key_value{ts, next_insert_index}, inserter);
        add_sample(
            "insert", next_insert_index, batch_size / insert_time.count());
        next_insert_index += batch_size;
        progress.update(batch_size);
        pre_fetch_hook();
        auto const fetch_time = time_block(
            batch_size, rand_existing_key{ts, next_insert_index - 1}, fetcher);
        add_sample("fetch", next_insert_index, batch_size / fetch_time.count());
        progress.update(batch_size);
    }
}

#if WITH_ROCKSDB
template<class AddSample>
void
do_timings_rocks(
    std::string const& db_dir,
    std::uint64_t batch_size,
    std::uint64_t num_batches,
    std::uint32_t key_size,
    AddSample&& add_sample,
    bench_progress& progress)
{
    temp_dir td{db_dir};
    std::unique_ptr<rocksdb::DB> pdb = [&td] {
        rocksdb::DB* db = nullptr;
        rocksdb::Options options;
        options.create_if_missing = true;
        auto const status = rocksdb::DB::Open(options, td.path(), &db);
        if (!status.ok())
            db = nullptr;
        return std::unique_ptr<rocksdb::DB>{db};
    }();

    if (!pdb)
    {
        derr << "Failed to open rocks db.\n";
        return;
    }

    auto inserter = [key_size, &pdb](item_type const& v) {
        auto const s = pdb->Put(rocksdb::WriteOptions(),
            rocksdb::Slice(reinterpret_cast<char const*>(v.key), key_size),
            rocksdb::Slice(reinterpret_cast<char const*>(v.data), v.size));
        if (!s.ok())
            throw std::runtime_error("Rocks Insert: " + s.ToString());
    };

    auto fetcher = [key_size, &pdb](item_type const& v) {
        std::string value;
        auto const s = pdb->Get(rocksdb::ReadOptions(),
            rocksdb::Slice(reinterpret_cast<char const*>(v.key), key_size),
            &value);
        if (!s.ok())
            throw std::runtime_error("Rocks Fetch: " + s.ToString());
    };

    test_store ts{key_size, 0, 0};
    try
    {
        time_fetch_insert_interleaved(batch_size, num_batches, ts,
            std::move(inserter), std::move(fetcher),
            std::forward<AddSample>(add_sample), [] {}, progress);
    }
    catch (std::exception const& e)
    {
        derr << "Error: " << e.what() << '\n';
    }
}
#endif

template <class AddSample>
void
do_timings(std::string const& db_dir,
    std::uint64_t batch_size,
    std::uint64_t num_batches,
    std::uint32_t key_size,
    std::size_t block_size,
    float load_factor,
    AddSample&& add_sample,
    bench_progress& progress)
{
    boost::system::error_code ec;

    try
    {
        test_store ts{db_dir, key_size, block_size, load_factor};
        ts.create(ec);
        if (ec)
            goto fail;
        ts.open(ec);
        if (ec)
            goto fail;

        auto inserter = [&ts, &ec](item_type const& v) {
            ts.db.insert(v.key, v.data, v.size, ec);
            if (ec)
                throw boost::system::system_error(ec);
        };

        auto fetcher = [&ts, &ec](item_type const& v) {
            ts.db.fetch(v.key, [&](void const* data, std::size_t size) {}, ec);
            if (ec)
                throw boost::system::system_error(ec);
        };

        auto pre_fetch_hook = [&ts, &ec]() {
            // Close then open the db otherwise the
            // commit thread confounds the timings
            ts.close(ec);
            if (ec)
                throw boost::system::system_error(ec);
            ts.open(ec);
            if (ec)
                throw boost::system::system_error(ec);
        };

        time_fetch_insert_interleaved(batch_size, num_batches, ts,
            std::move(inserter), std::move(fetcher),
            std::forward<AddSample>(add_sample), std::move(pre_fetch_hook),
            progress);
    }
    catch (boost::system::system_error const& e)
    {
        ec = e.code();
    }
    catch (std::exception const& e)
    {
        derr << "Error: " << e.what() << '\n';
    }

fail:
    if (ec)
        derr << "Error: " << ec.message() << '\n';

    return;
}

namespace po = boost::program_options;

void
print_help(std::string const& prog_name, const po::options_description& desc)
{
    derr << prog_name << ' ' << desc;
}

po::variables_map
parse_args(int argc, char** argv, po::options_description& desc)
{

#if WITH_ROCKSDB
    std::vector<std::string> const default_dbs = {"nudb", "rocksdb"};
#else
    std::vector<std::string> const default_dbs = {"nudb"};
#endif
    std::vector<std::uint64_t> const default_ops({100000,1000000});

    desc.add_options()
        ("help,h", "Display this message.")
        ("batch_size",
         po::value<std::uint64_t>(),
         "Batch Size Default: 20000)")
        ("num_batches",
         po::value<std::uint64_t>(),
         "Num Batches Default: 500)")
        ("dbs",
         po::value<std::vector<std::string>>()->multitoken(),
          "databases (Default: nudb rocksdb)")
        ("block_size", po::value<size_t>(),
         "nudb block size (default: 4096)")
        ("key_size", po::value<size_t>(),
         "key size (default: 64)")
        ("load_factor", po::value<float>(),
         "nudb load factor (default: 0.5)")
        ("db_dir", po::value<std::string>(),
         "Directory to place the databases"
         " (default: boost::filesystem::temp_directory_path)")
        ("raw_out", po::value<std::string>(),
         "File to record the raw measurements (useful for plotting)"
         " (default: no output)")
          ;

        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
        po::notify(vm);

        return vm;
}

template<class T>
T
get_opt(po::variables_map const& vm, std::string const& key, T const& default_value)
{
    return vm.count(key) ? vm[key].as<T>() : default_value;
}

} // test
} // nudb

int
main(int argc, char** argv)
{
    using namespace nudb::test;

    po::variables_map vm;

    {
        po::options_description desc{"Benchmark Options"};
        bool parse_error = false;
        try
        {
            vm = parse_args(argc, argv, desc);
        }
        catch (std::exception const& e)
        {
            derr << "Incorrect command line syntax.\n";
            derr << "Exception: " << e.what() << '\n';
            parse_error = true;
        }

        if (vm.count("help") || parse_error)
        {
            auto prog_name = boost::filesystem::path(argv[0]).stem().string();
            print_help(prog_name, desc);
            return 0;
        }
    }

    auto const batch_size = get_opt<size_t>(vm, "batch_size", 20000);
    auto const num_batches = get_opt<size_t>(vm, "num_batches", 500);
    auto const block_size = get_opt<size_t>(vm, "block_size", 4096);
    auto const load_factor = get_opt<float>(vm, "load_factor", 0.5f);
    auto const key_size = get_opt<size_t>(vm, "key_size", 64);
    auto const db_dir = [&vm]() -> std::string {
        auto r = get_opt<std::string>(vm, "db_dir", "");
        if (!r.empty() && r.back() != '/' && r.back() != '\\')
        {
            r += '/';
        }
        return r;
    }();
    auto const raw_out = get_opt<std::string>(vm, "raw_out", "");
#if WITH_ROCKSDB
    std::vector<std::string> const default_dbs({"nudb", "rocksdb"});
#else
    std::vector<std::string> const default_dbs({"nudb"});
#endif
    auto to_set = [](std::vector<std::string> const& v) {
        return std::set<std::string>(v.begin(), v.end());
    };
    auto const dbs = to_set(get_opt<std::vector<std::string>>(vm, "dbs", default_dbs));

    for (auto const& db : dbs)
    {
        if (db == "rocksdb")
        {
#if !WITH_ROCKSDB
            derr << "Benchmark was not built with rocksdb support\n";
            exit(1);
#endif
            continue;
        }

        if (db != "nudb" && db != "rocksdb")
        {
            derr << "Unsupported database: " << db << '\n';
            exit(1);
        }
    }

    bool const with_rocksdb = dbs.count("rocksdb") != 0;
    (void) with_rocksdb;
    bool const with_nudb = dbs.count("nudb") != 0;
    std::uint64_t const num_db = int(with_nudb) + int(with_rocksdb);
    std::uint64_t const total_ops = num_db * batch_size * num_batches * 2;
    bench_progress progress(derr, total_ops);

    enum
    {
        db_nudb,
        db_rocks,
        db_last
    };
    enum
    {
        op_insert,
        op_fetch,
        op_last
    };
    std::array<std::string, db_last> db_names{{"nudb", "rocksdb"}};
    std::array<std::string, db_last> op_names{{"insert", "fetch"}};
    using result_dict = boost::container::flat_multimap<std::uint64_t, double>;
    result_dict ops_per_sec[db_last][op_last];
    // Reserve up front to database that run later don't have less memory
    for (int i = 0; i < db_last; ++i)
        for (int j = 0; j < op_last; ++j)
            ops_per_sec[i][j].reserve(num_batches);

    std::ofstream raw_out_stream;
    bool const record_raw_out = !raw_out.empty();
    if (record_raw_out)
    {
        raw_out_stream.open(raw_out, std::ios::trunc);
        raw_out_stream << "num_db_items,db,op,ops/sec\n";
    }
    for (int i = 0; i < db_last; ++i)
    {
        auto result = [&]
            (std::string const& op_name, std::uint64_t num_items,
             double sample) {
            auto op_idx = op_name == "insert" ? op_insert : op_fetch;
            ops_per_sec[i][op_idx].emplace(num_items, sample);
            if (record_raw_out)
                raw_out_stream << num_items << ',' << db_names[i] << ','
                               << op_name << ',' << std::fixed << sample
                               << std::endl;  // flush

        };
        if (with_nudb && i == db_nudb)
            do_timings(db_dir, batch_size, num_batches, key_size, block_size,
                load_factor, result, progress);
#if WITH_ROCKSDB
        if (with_rocksdb && i == db_rocks)
            do_timings_rocks(
                db_dir, batch_size, num_batches, key_size, result, progress);
#endif
    }

    // Write summary by sampling raw data at powers of 10
    auto const col_w = 14;
    auto const iter_w = 15;

    for (int op_idx = 0; op_idx < op_last; ++op_idx)
    {
        auto const& t = op_names[op_idx];
        dout << '\n' << t << " (per second)\n";
        dout << std::setw(iter_w) << "num_db_keys";
        if (with_nudb)
            dout << std::setw(col_w) << "nudb";
#if WITH_ROCKSDB
        if (with_rocksdb)
            dout << std::setw(col_w) << "rocksdb";
#endif
        dout << '\n';
        auto const max_sample = [&ops_per_sec] {
            std::uint64_t r = 0;
            for (auto i = 0; i < db_last; ++i)
                for (auto j = 0; j < op_last; ++j)
                    if (!ops_per_sec[i][j].empty())
                        r = std::max(r, ops_per_sec[i][j].rbegin()->first); // no `back()`
            return r;
        }();
        auto const min_sample = batch_size;

        auto write_val = [&](
            result_dict const& dict, std::uint64_t key) {
            dout << std::setw(col_w) << std::fixed << std::setprecision(2);
            // Take the average of all the values, or "NA" if none collected
            auto l = dict.lower_bound(key);
            auto u = dict.upper_bound(key);
            if (l == u)
                dout << "NA";
            else
            {
                auto const total = std::accumulate(l, u, 0,
                    [](double a, std::pair<std::uint64_t, double> const& b) {
                        return a + b.second;
                    });
                dout << total / std::distance(l, u);
            }
        };
        for (std::uint64_t n = 100; n <= max_sample; n *= 10)
        {
            if (n<min_sample)
                continue;
            dout << std::setw(iter_w) << n;
            if (with_nudb)
                write_val(ops_per_sec[db_nudb][op_idx], n);
#if WITH_ROCKSDB
            if (with_rocksdb)
                write_val(ops_per_sec[db_rocks][op_idx], n);
#endif
            dout << '\n';
        }
    }
}
