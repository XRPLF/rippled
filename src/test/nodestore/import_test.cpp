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

#include <BeastConfig.h>
#include <ripple/basics/contract.h>
#include <ripple/nodestore/impl/codec.h>
#include <ripple/beast/clock/basic_seconds_clock.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/unit_test.h>
#include <nudb/create.hpp>
#include <nudb/detail/format.hpp>
#include <nudb/xxhasher.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <sstream>

#include <ripple/unity/rocksdb.h>

/*

Math:

1000 gb dat file
170 gb key file
capacity 113 keys/bucket

normal:
1,000gb data file read
19,210gb key file read (113 * 170)
19,210gb key file write

multi(32gb):
6 passes (170/32)
6,000gb data file read
170gb key file write


*/

namespace ripple {
namespace NodeStore {

namespace detail {

class save_stream_state
{
    std::ostream& os_;
    std::streamsize precision_;
    std::ios::fmtflags flags_;
    std::ios::char_type fill_;
public:
    ~save_stream_state()
    {
        os_.precision(precision_);
        os_.flags(flags_);
        os_.fill(fill_);
    }
    save_stream_state(save_stream_state const&) = delete;
    save_stream_state& operator=(save_stream_state const&) = delete;
    explicit save_stream_state(std::ostream& os)
        : os_(os)
        , precision_(os.precision())
        , flags_(os.flags())
        , fill_(os.fill())
    {
    }
};

template <class Rep, class Period>
std::ostream&
pretty_time(std::ostream& os, std::chrono::duration<Rep, Period> d)
{
    save_stream_state _(os);
    using namespace std::chrono;
    if (d < microseconds{1})
    {
        // use nanoseconds
        if (d < nanoseconds{100})
        {
            // use floating
            using ns = duration<float, std::nano>;
            os << std::fixed << std::setprecision(1) << ns(d).count();
        }
        else
        {
            // use integral
            os << round<nanoseconds>(d).count();
        }
        os << "ns";
    }
    else if (d < milliseconds{1})
    {
        // use microseconds
        if (d < microseconds{100})
        {
            // use floating
            using ms = duration<float, std::micro>;
            os << std::fixed << std::setprecision(1) << ms(d).count();
        }
        else
        {
            // use integral
            os << round<microseconds>(d).count();
        }
        os << "us";
    }
    else if (d < seconds{1})
    {
        // use milliseconds
        if (d < milliseconds{100})
        {
            // use floating
            using ms = duration<float, std::milli>;
            os << std::fixed << std::setprecision(1) << ms(d).count();
        }
        else
        {
            // use integral
            os << round<milliseconds>(d).count();
        }
        os << "ms";
    }
    else if (d < minutes{1})
    {
        // use seconds
        if (d < seconds{100})
        {
            // use floating
            using s = duration<float>;
            os << std::fixed << std::setprecision(1) << s(d).count();
        }
        else
        {
            // use integral
            os << round<seconds>(d).count();
        }
        os << "s";
    }
    else
    {
        // use minutes
        if (d < minutes{100})
        {
            // use floating
            using m = duration<float, std::ratio<60>>;
            os << std::fixed << std::setprecision(1) << m(d).count();
        }
        else
        {
            // use integral
            os << round<minutes>(d).count();
        }
        os << "min";
    }
    return os;
}

template <class Period, class Rep>
inline
std::string
fmtdur(std::chrono::duration<Period, Rep> const& d)
{
    std::stringstream ss;
    pretty_time(ss, d);
    return ss.str();
}

} // detail

//------------------------------------------------------------------------------

class progress
{
private:
    using clock_type =
        beast::basic_seconds_clock<
            std::chrono::steady_clock>;

    std::size_t const work_;
    clock_type::time_point start_ = clock_type::now();
    clock_type::time_point now_ = clock_type::now();
    clock_type::time_point report_ = clock_type::now();
    std::size_t prev_ = 0;
    bool estimate_ = false;

public:
    explicit
    progress(std::size_t work)
        : work_(work)
    {
    }

    template <class Log>
    void
    operator()(Log& log, std::size_t work)
    {
        using namespace std::chrono;
        auto const now = clock_type::now();
        if (now == now_)
            return;
        now_ = now;
        auto const elapsed = now - start_;
        if (! estimate_)
        {
            if (elapsed < seconds(15))
                return;
            estimate_ = true;
        }
        else if (now - report_ <
            std::chrono::seconds(60))
        {
            return;
        }
        auto const rate =
            elapsed.count() / double(work);
        clock_type::duration const remain(
            static_cast<clock_type::duration::rep>(
                (work_ - work) * rate));
        log <<
            "Remaining: " << detail::fmtdur(remain) <<
                " (" << work << " of " << work_ <<
                    " in " << detail::fmtdur(elapsed) <<
                ", " << (work - prev_) <<
                    " in " << detail::fmtdur(now - report_) <<
                ")";
        report_ = now;
        prev_ = work;
    }

    template <class Log>
    void
    finish(Log& log)
    {
        log <<
            "Total time: " << detail::fmtdur(
                clock_type::now() - start_);
    }
};

std::map <std::string, std::string, boost::beast::iless>
parse_args(std::string const& s)
{
    // <key> '=' <value>
    static boost::regex const re1 (
        "^"                         // start of line
        "(?:\\s*)"                  // whitespace (optonal)
        "([a-zA-Z][_a-zA-Z0-9]*)"   // <key>
        "(?:\\s*)"                  // whitespace (optional)
        "(?:=)"                     // '='
        "(?:\\s*)"                  // whitespace (optional)
        "(.*\\S+)"                  // <value>
        "(?:\\s*)"                  // whitespace (optional)
        , boost::regex_constants::optimize
    );
    std::map <std::string,
        std::string, boost::beast::iless> map;
    auto const v = beast::rfc2616::split(
        s.begin(), s.end(), ',');
    for (auto const& kv : v)
    {
        boost::smatch m;
        if (! boost::regex_match (kv, m, re1))
            Throw<std::runtime_error> (
                "invalid parameter " + kv);
        auto const result =
            map.emplace(m[1], m[2]);
        if (! result.second)
            Throw<std::runtime_error> (
                "duplicate parameter " + m[1]);
    }
    return map;
}

//------------------------------------------------------------------------------

#if RIPPLE_ROCKSDB_AVAILABLE

class import_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        testcase(beast::unit_test::abort_on_fail) << arg();

        using namespace nudb;
        using namespace nudb::detail;

        pass();
        auto const args = parse_args(arg());
        bool usage = args.empty();

        if (! usage &&
            args.find("from") == args.end())
        {
            log <<
                "Missing parameter: from";
            usage = true;
        }
        if (! usage &&
            args.find("to") == args.end())
        {
            log <<
                "Missing parameter: to";
            usage = true;
        }
        if (! usage &&
            args.find("buffer") == args.end())
        {
            log <<
                "Missing parameter: buffer";
            usage = true;
        }

        if (usage)
        {
            log <<
                "Usage:\n" <<
                "--unittest-arg=from=<from>,to=<to>,buffer=<buffer>\n" <<
                "from:   RocksDB database to import from\n" <<
                "to:     NuDB database to import to\n" <<
                "buffer: Buffer size (bigger is faster)\n" <<
                "NuDB database must not already exist.";
            return;
        }

        // This controls the size of the bucket buffer.
        // For a 1TB data file, a 32GB bucket buffer is suggested.
        // The larger the buffer, the faster the import.
        //
        std::size_t const buffer_size =
            std::stoull(args.at("buffer"));
        auto const from_path = args.at("from");
        auto const to_path = args.at("to");

        using hash_type = nudb::xxhasher;
        auto const bulk_size = 64 * 1024 * 1024;
        float const load_factor = 0.5;

        auto const dp = to_path + ".dat";
        auto const kp = to_path + ".key";

        auto const start =
            std::chrono::steady_clock::now();

        log <<
            "from:    " << from_path << "\n"
            "to:      " << to_path << "\n"
            "buffer:  " << buffer_size;

        std::unique_ptr<rocksdb::DB> db;
        {
            rocksdb::Options options;
            options.create_if_missing = false;
            options.max_open_files = 2000; // 5000?
            rocksdb::DB* pdb = nullptr;
            rocksdb::Status status =
                rocksdb::DB::OpenForReadOnly(
                    options, from_path, &pdb);
            if (! status.ok () || ! pdb)
                Throw<std::runtime_error> (
                    "Can't open '" + from_path + "': " +
                        status.ToString());
            db.reset(pdb);
        }
        // Create data file with values
        std::size_t nitems = 0;
        std::size_t nbytes = 0;
        dat_file_header dh;
        dh.version = currentVersion;
        dh.uid = make_uid();
        dh.appnum = 1;
        dh.key_size = 32;

        native_file df;
        error_code ec;
        df.create(file_mode::append, dp, ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        bulk_writer<native_file> dw(
            df, 0, bulk_size);
        {
            {
                auto os = dw.prepare(dat_file_header::size, ec);
                if (ec)
                    Throw<nudb::system_error>(ec);
                write(os, dh);
            }
            rocksdb::ReadOptions options;
            options.verify_checksums = false;
            options.fill_cache = false;
            std::unique_ptr<rocksdb::Iterator> it(
                db->NewIterator(options));

            buffer buf;
            for (it->SeekToFirst (); it->Valid (); it->Next())
            {
                if (it->key().size() != 32)
                    Throw<std::runtime_error> (
                        "Unexpected key size " +
                            std::to_string(it->key().size()));
                void const* const key = it->key().data();
                void const* const data = it->value().data();
                auto const size = it->value().size();
                std::unique_ptr<char[]> clean(
                    new char[size]);
                std::memcpy(clean.get(), data, size);
                filter_inner(clean.get(), size);
                auto const out = nodeobject_compress(
                    clean.get(), size, buf);
                // Verify codec correctness
                {
                    buffer buf2;
                    auto const check = nodeobject_decompress(
                        out.first, out.second, buf2);
                    BEAST_EXPECT(check.second == size);
                    BEAST_EXPECT(std::memcmp(
                        check.first, clean.get(), size) == 0);
                }
                // Data Record
                auto os = dw.prepare(
                    field<uint48_t>::size + // Size
                    32 +                    // Key
                    out.second, ec);
                if (ec)
                    Throw<nudb::system_error>(ec);
                write<uint48_t>(os, out.second);
                std::memcpy(os.data(32), key, 32);
                std::memcpy(os.data(out.second),
                    out.first, out.second);
                ++nitems;
                nbytes += size;
            }
            dw.flush(ec);
            if (ec)
                Throw<nudb::system_error>(ec);
        }
        db.reset();
        log <<
            "Import data: " << detail::fmtdur(
                std::chrono::steady_clock::now() - start);
        auto const df_size = df.size(ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        // Create key file
        key_file_header kh;
        kh.version = currentVersion;
        kh.uid = dh.uid;
        kh.appnum = dh.appnum;
        kh.key_size = 32;
        kh.salt = make_salt();
        kh.pepper = pepper<hash_type>(kh.salt);
        kh.block_size = block_size(kp);
        kh.load_factor = std::min<std::size_t>(
            65536.0 * load_factor, 65535);
        kh.buckets = std::ceil(nitems / (bucket_capacity(
            kh.block_size) * load_factor));
        kh.modulus = ceil_pow2(kh.buckets);
        native_file kf;
        kf.create(file_mode::append, kp, ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        buffer buf(kh.block_size);
        {
            std::memset(buf.get(), 0, kh.block_size);
            ostream os(buf.get(), kh.block_size);
            write(os, kh);
            kf.write(0, buf.get(), kh.block_size, ec);
            if (ec)
                Throw<nudb::system_error>(ec);
        }
        // Build contiguous sequential sections of the
        // key file using multiple passes over the data.
        //
        auto const buckets = std::max<std::size_t>(1,
            buffer_size / kh.block_size);
        buf.reserve(buckets * kh.block_size);
        auto const passes =
            (kh.buckets + buckets - 1) / buckets;
        log <<
            "items:   " << nitems << "\n"
            "buckets: " << kh.buckets << "\n"
            "data:    " << df_size << "\n"
            "passes:  " << passes;
        progress p(df_size * passes);
        std::size_t npass = 0;
        for (std::size_t b0 = 0; b0 < kh.buckets;
                b0 += buckets)
        {
            auto const b1 = std::min(
                b0 + buckets, kh.buckets);
            // Buffered range is [b0, b1)
            auto const bn = b1 - b0;
            // Create empty buckets
            for (std::size_t i = 0; i < bn; ++i)
            {
                bucket b(kh.block_size,
                    buf.get() + i * kh.block_size,
                        empty);
            }
            // Insert all keys into buckets
            // Iterate Data File
            bulk_reader<native_file> r(
                df, dat_file_header::size,
                    df_size, bulk_size);
            while (! r.eof())
            {
                auto const offset = r.offset();
                // Data Record or Spill Record
                std::size_t size;
                auto is = r.prepare(
                    field<uint48_t>::size, ec); // Size
                if (ec)
                    Throw<nudb::system_error>(ec);
                read<uint48_t>(is, size);
                if (size > 0)
                {
                    // Data Record
                    is = r.prepare(
                        dh.key_size +           // Key
                        size, ec);                  // Data
                    if (ec)
                        Throw<nudb::system_error>(ec);
                    std::uint8_t const* const key =
                        is.data(dh.key_size);
                    auto const h = hash<hash_type>(
                        key, kh.key_size, kh.salt);
                    auto const n = bucket_index(
                        h, kh.buckets, kh.modulus);
                    p(log,
                        npass * df_size + r.offset());
                    if (n < b0 || n >= b1)
                        continue;
                    bucket b(kh.block_size, buf.get() +
                        (n - b0) * kh.block_size);
                    maybe_spill(b, dw, ec);
                    if (ec)
                        Throw<nudb::system_error>(ec);
                    b.insert(offset, size, h);
                }
                else
                {
                    // VFALCO Should never get here
                    // Spill Record
                    is = r.prepare(
                        field<std::uint16_t>::size, ec);
                    if (ec)
                        Throw<nudb::system_error>(ec);
                    read<std::uint16_t>(is, size);  // Size
                    r.prepare(size, ec); // skip
                    if (ec)
                        Throw<nudb::system_error>(ec);
                }
            }
            kf.write((b0 + 1) * kh.block_size,
                buf.get(), bn * kh.block_size, ec);
            if (ec)
                Throw<nudb::system_error>(ec);
            ++npass;
        }
        dw.flush(ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        p.finish(log);
    }
};

BEAST_DEFINE_TESTSUITE(import,NodeStore,ripple);

#endif

//------------------------------------------------------------------------------

} // NodeStore
} // ripple

