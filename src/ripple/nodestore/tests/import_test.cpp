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
#include <beast/hash/xxhasher.h>
#include <ripple/basics/contract.h>
#include <ripple/nodestore/impl/codec.h>
#include <beast/chrono/basic_seconds_clock.h>
#include <beast/chrono/chrono_io.h>
#include <beast/http/rfc2616.h>
#include <beast/nudb/create.h>
#include <beast/nudb/detail/format.h>
#include <beast/unit_test/suite.h>
#include <beast/utility/ci_char_traits.h>
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
            os << std::chrono::round<nanoseconds>(d).count();
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
            os << std::chrono::round<microseconds>(d).count();
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
            os << std::chrono::round<milliseconds>(d).count();
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
            os << std::chrono::round<seconds>(d).count();
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
            os << std::chrono::round<minutes>(d).count();
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

std::map <std::string, std::string, beast::ci_less>
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
        std::string, beast::ci_less> map;
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
        testcase(abort_on_fail) << arg();

        using namespace beast::nudb;
        using namespace beast::nudb::detail;

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

        using hash_type = beast::xxhasher;
        using codec_type = nodeobject_codec;
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
        df.create(file_mode::append, dp);
        bulk_writer<native_file> dw(
            df, 0, bulk_size);
        {
            {
                auto os = dw.prepare(dat_file_header::size);
                write(os, dh);
            }
            rocksdb::ReadOptions options;
            options.verify_checksums = false;
            options.fill_cache = false;
            std::unique_ptr<rocksdb::Iterator> it(
                db->NewIterator(options));

            buffer buf;
            codec_type codec;
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
                auto const out = codec.compress(
                    clean.get(), size, buf);
                // Verify codec correctness
                {
                    buffer buf2;
                    auto const check = codec.decompress(
                        out.first, out.second, buf2);
                    expect(check.second == size,
                        "codec size error");
                    expect(std::memcmp(
                        check.first, clean.get(), size) == 0,
                            "codec data error");
                }
                // Data Record
                auto os = dw.prepare(
                    field<uint48_t>::size + // Size
                    32 +                    // Key
                    out.second);
                write<uint48_t>(os, out.second);
                std::memcpy(os.data(32), key, 32);
                std::memcpy(os.data(out.second),
                    out.first, out.second);
                ++nitems;
                nbytes += size;
            }
            dw.flush();
        }
        db.reset();
        log <<
            "Import data: " << detail::fmtdur(
                std::chrono::steady_clock::now() - start);
        auto const df_size =
            df.actual_size();
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
        kf.create(file_mode::append, kp);
        buffer buf(kh.block_size);
        {
            std::memset(buf.get(), 0, kh.block_size);
            ostream os(buf.get(), kh.block_size);
            write(os, kh);
            kf.write(0, buf.get(), kh.block_size);
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
                    field<uint48_t>::size); // Size
                read<uint48_t>(is, size);
                if (size > 0)
                {
                    // Data Record
                    is = r.prepare(
                        dh.key_size +           // Key
                        size);                  // Data
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
                    maybe_spill(b, dw);
                    b.insert(offset, size, h);
                }
                else
                {
                    // VFALCO Should never get here
                    // Spill Record
                    is = r.prepare(
                        field<std::uint16_t>::size);
                    read<std::uint16_t>(is, size);  // Size
                    r.prepare(size); // skip
                }
            }
            kf.write((b0 + 1) * kh.block_size,
                buf.get(), bn * kh.block_size);
            ++npass;
        }
        dw.flush();
        p.finish(log);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(import,NodeStore,ripple);

#endif

//------------------------------------------------------------------------------

class rekey_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        testcase(abort_on_fail) << arg();

        using namespace beast::nudb;
        using namespace beast::nudb::detail;

        pass();
        auto const args = parse_args(arg());
        bool usage = args.empty();

        if (! usage &&
            args.find("path") == args.end())
        {
            log <<
                "Missing parameter: path";
            usage = true;
        }
        if (! usage &&
            args.find("items") == args.end())
        {
            log <<
                "Missing parameter: items";
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
                "--unittest-arg=path=<path>,items=<items>,buffer=<buffer>\n" <<
                "path:   NuDB path to rekey (without the .dat)\n" <<
                "items:  Number of items in the .dat file\n" <<
                "buffer: Buffer size (bigger is faster)\n" <<
                "NuDB key file must not already exist.";
            return;
        }

        std::size_t const buffer_size =
            std::stoull(args.at("buffer"));
        auto const path = args.at("path");
        std::size_t const items =
            std::stoull(args.at("items"));

        using hash_type = beast::xxhasher;
        auto const bulk_size = 64 * 1024 * 1024;
        float const load_factor = 0.5;

        auto const dp = path + ".dat";
        auto const kp = path + ".key";

        log <<
            "path:   " << path << "\n"
            "items:  " << items << "\n"
            "buffer: " << buffer_size;

        // Create data file with values
        native_file df;
        df.open(file_mode::append, dp);
        dat_file_header dh;
        read(df, dh);
        auto const df_size = df.actual_size();
        bulk_writer<native_file> dw(
            df, df_size, bulk_size);

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
        kh.buckets = std::ceil(items / (bucket_capacity(
            kh.block_size) * load_factor));
        kh.modulus = ceil_pow2(kh.buckets);
        native_file kf;
        kf.create(file_mode::append, kp);
        buffer buf(kh.block_size);
        {
            std::memset(buf.get(), 0, kh.block_size);
            ostream os(buf.get(), kh.block_size);
            write(os, kh);
            kf.write(0, buf.get(), kh.block_size);
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
                    field<uint48_t>::size); // Size
                read<uint48_t>(is, size);
                if (size > 0)
                {
                    // Data Record
                    is = r.prepare(
                        dh.key_size +           // Key
                        size);                  // Data
                    std::uint8_t const* const key =
                        is.data(dh.key_size);
                    auto const h = hash<hash_type>(
                        key, dh.key_size, kh.salt);
                    auto const n = bucket_index(
                        h, kh.buckets, kh.modulus);
                    p(log,
                        npass * df_size + r.offset());
                    if (n < b0 || n >= b1)
                        continue;
                    bucket b(kh.block_size, buf.get() +
                        (n - b0) * kh.block_size);
                    maybe_spill(b, dw);
                    b.insert(offset, size, h);
                }
                else
                {
                    // VFALCO Should never get here
                    // Spill Record
                    is = r.prepare(
                        field<std::uint16_t>::size);
                    read<std::uint16_t>(is, size);  // Size
                    r.prepare(size); // skip
                }
            }
            kf.write((b0 + 1) * kh.block_size,
                buf.get(), bn * kh.block_size);
            ++npass;
        }
        dw.flush();
        p.finish(log);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(rekey,NodeStore,ripple);

//------------------------------------------------------------------------------

namespace legacy {

using namespace beast::nudb;
using namespace beast::nudb::detail;

struct dat_file_header
{
    static std::size_t BEAST_CONSTEXPR size =
        8 +     // Type
        2 +     // Version
        8 +     // Appnum
        8 +     // Salt
        2 +     // KeySize
        64;     // (Reserved)

    char type[8];
    std::size_t version;
    std::uint64_t appnum;
    std::uint64_t salt;
    std::size_t key_size;
};

struct key_file_header
{
    static std::size_t BEAST_CONSTEXPR size =
        8 +     // Type
        2 +     // Version
        8 +     // Appnum
        8 +     // Salt
        8 +     // Pepper
        2 +     // KeySize
        2 +     // BlockSize
        2 +     // LoadFactor
        64;     // (Reserved)

    char type[8];
    std::size_t version;
    std::uint64_t appnum;
    std::uint64_t salt;
    std::uint64_t pepper;
    std::size_t key_size;
    std::size_t block_size;
    std::size_t load_factor;

    // Computed values
    std::size_t capacity;
    std::size_t bucket_size;
    std::size_t buckets;
    std::size_t modulus;
};

// Read data file header from stream
template <class = void>
void
read (istream& is, dat_file_header& dh)
{
    read (is, dh.type, sizeof(dh.type));
    read<std::uint16_t>(is, dh.version);
    read<std::uint64_t>(is, dh.appnum);
    read<std::uint64_t>(is, dh.salt);
    read<std::uint16_t>(is, dh.key_size);
    std::array <std::uint8_t, 64> zero;
    read (is, zero.data(), zero.size());
}

// Read data file header from file
template <class File>
void
read (File& f, dat_file_header& dh)
{
    std::array<std::uint8_t,
        dat_file_header::size> buf;
    try
    {
        f.read(0, buf.data(), buf.size());
    }
    catch (file_short_read_error const&)
    {
        Throw<store_corrupt_error> (
            "short data file header");
    }
    istream is(buf);
    read (is, dh);
}

// Read key file header from stream
template <class = void>
void
read (istream& is, std::size_t file_size,
    key_file_header& kh)
{
    read(is, kh.type, sizeof(kh.type));
    read<std::uint16_t>(is, kh.version);
    read<std::uint64_t>(is, kh.appnum);
    read<std::uint64_t>(is, kh.salt);
    read<std::uint64_t>(is, kh.pepper);
    read<std::uint16_t>(is, kh.key_size);
    read<std::uint16_t>(is, kh.block_size);
    read<std::uint16_t>(is, kh.load_factor);
    std::array <std::uint8_t, 64> zero;
    read (is, zero.data(), zero.size());

    // VFALCO These need to be checked to handle
    //        when the file size is too small
    kh.capacity = bucket_capacity(kh.block_size);
    kh.bucket_size = bucket_size(kh.capacity);
    if (file_size > kh.block_size)
    {
        // VFALCO This should be handled elsewhere.
        //        we shouldn't put the computed fields in this header.
        if (kh.block_size > 0)
            kh.buckets = (file_size - kh.bucket_size)
                / kh.block_size;
        else
            // VFALCO Corruption or logic error
            kh.buckets = 0;
    }
    else
    {
        kh.buckets = 0;
    }
    kh.modulus = ceil_pow2(kh.buckets);
}

// Read key file header from file
template <class File>
void
read (File& f, key_file_header& kh)
{
    std::array <std::uint8_t,
        key_file_header::size> buf;
    try
    {
        f.read(0, buf.data(), buf.size());
    }
    catch (file_short_read_error const&)
    {
        Throw<store_corrupt_error> (
            "short key file header");
    }
    istream is(buf);
    read (is, f.actual_size(), kh);
}

} // detail

class update_test : public beast::unit_test::suite
{
public:
    void
    run() override
    {
        testcase(abort_on_fail) << arg();

        using namespace beast::nudb;
        using namespace beast::nudb::detail;

        pass();
        auto const args = parse_args(arg());
        bool usage = args.empty();

        if (! usage &&
            args.find("path") == args.end())
        {
            log <<
                "Missing parameter: path";
            usage = true;
        }

        if (usage)
        {
            log <<
                "Usage:\n" <<
                "--unittest-arg=path=<dat>\n" <<
                "path:   NuDB path to update (without extensions)";
            return;
        }

        auto const path = args.at("path");

        using hash_type = beast::xxhasher;

        auto const dp = path + ".dat";
        auto const kp = path + ".key";

        log <<
            "path:   " << path;

        native_file df;
        native_file kf;
        df.open(file_mode::write, dp);
        kf.open(file_mode::write, kp);
        legacy::dat_file_header dh0;
        legacy::key_file_header kh0;
        read(df, dh0);
        read(kf, kh0);

        dat_file_header dh;
        std::memcpy(dh.type, "nudb.dat", 8);
        dh.version = dh0.version;;
        dh.uid = make_uid();
        dh.appnum = dh0.appnum;
        dh.key_size = dh0.key_size;

        key_file_header kh;
        std::memcpy(kh.type, "nudb.key", 8);
        kh.version = dh.version;
        kh.uid = dh.uid;
        kh.appnum = dh.appnum;
        kh.key_size = dh.key_size;
        kh.salt = kh0.salt;
        kh.pepper = kh0.pepper;
        kh.block_size = kh0.block_size;
        kh.load_factor = kh0.load_factor;

        // VFALCO These need to be checked to handle
        //        when the file size is too small
        kh.capacity = bucket_capacity(kh.block_size);
        kh.bucket_size = bucket_size(kh.capacity);
        auto const kf_size = kf.actual_size();
        if (kf_size > kh.block_size)
        {
            // VFALCO This should be handled elsewhere.
            //        we shouldn't put the computed fields
            //        in this header.
            if (kh.block_size > 0)
                kh.buckets = (kf_size - kh.bucket_size)
                    / kh.block_size;
            else
                // VFALCO Corruption or logic error
                kh.buckets = 0;
        }
        else
        {
            kh.buckets = 0;
        }
        kh.modulus = ceil_pow2(kh.buckets);
        verify(dh);
        verify<hash_type>(dh, kh);
        write(df, dh);
        write(kf, kh);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(update,NodeStore,ripple);

}
}
