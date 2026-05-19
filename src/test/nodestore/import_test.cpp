#include <xrpl/basics/contract.h>
#include <xrpl/beast/clock/basic_seconds_clock.h>
#include <xrpl/beast/rfc2616.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/nodestore/detail/codec.h>

#include <boost/beast/core/string.hpp>
#include <boost/regex.hpp>  // IWYU pragma: keep
#include <boost/regex/v5/regbase.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_match.hpp>

#include <nudb/create.hpp>  // IWYU pragma: keep
#include <nudb/detail/bucket.hpp>
#include <nudb/detail/buffer.hpp>
#include <nudb/detail/bulkio.hpp>
#include <nudb/detail/field.hpp>
#include <nudb/detail/format.hpp>
#include <nudb/detail/stream.hpp>
#include <nudb/error.hpp>
#include <nudb/file.hpp>
#include <nudb/native_file.hpp>
#include <nudb/xxhasher.hpp>
#include <rocksdb/db.h>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/status.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <ios>
#include <map>
#include <memory>
#include <ostream>
#include <ratio>
#include <sstream>
#include <stdexcept>
#include <string>

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

namespace xrpl {

namespace detail {

class SaveStreamState
{
    std::ostream& os_;
    std::streamsize precision_;
    std::ios::fmtflags flags_;
    std::ios::char_type fill_;

public:
    ~SaveStreamState()
    {
        os_.precision(precision_);
        os_.flags(flags_);
        os_.fill(fill_);
    }
    SaveStreamState(SaveStreamState const&) = delete;
    SaveStreamState&
    operator=(SaveStreamState const&) = delete;
    explicit SaveStreamState(std::ostream& os)
        : os_(os), precision_(os.precision()), flags_(os.flags()), fill_(os.fill())
    {
    }
};

template <class Rep, class Period>
std::ostream&
prettyTime(std::ostream& os, std::chrono::duration<Rep, Period> d)
{
    SaveStreamState const _(os);
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
inline std::string
fmtdur(std::chrono::duration<Period, Rep> const& d)
{
    std::stringstream ss;
    prettyTime(ss, d);
    return ss.str();
}

}  // namespace detail

namespace NodeStore {

//------------------------------------------------------------------------------

class Progress
{
private:
    using clock_type = beast::BasicSecondsClock;

    std::size_t const work_;
    clock_type::time_point start_ = clock_type::now();
    clock_type::time_point now_ = clock_type::now();
    clock_type::time_point report_ = clock_type::now();
    std::size_t prev_ = 0;
    bool estimate_ = false;

public:
    explicit Progress(std::size_t work) : work_(work)
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
        if (!estimate_)
        {
            if (elapsed < seconds(15))
                return;
            estimate_ = true;
        }
        else if (now - report_ < std::chrono::seconds(60))
        {
            return;
        }
        auto const rate = elapsed.count() / double(work);
        clock_type::duration const remain(
            static_cast<clock_type::duration::rep>((work_ - work) * rate));
        log << "Remaining: " << detail::fmtdur(remain) << " (" << work << " of " << work_ << " in "
            << detail::fmtdur(elapsed) << ", " << (work - prev_) << " in "
            << detail::fmtdur(now - report_) << ")";
        report_ = now;
        prev_ = work;
    }

    template <class Log>
    void
    finish(Log& log)
    {
        log << "Total time: " << detail::fmtdur(clock_type::now() - start_);
    }
};

std::map<std::string, std::string, boost::beast::iless>
parseArgs(std::string const& s)
{
    // <key> '=' <value>
    static boost::regex const kRe1(
        "^"                        // start of line
        "(?:\\s*)"                 // whitespace (optional)
        "([a-zA-Z][_a-zA-Z0-9]*)"  // <key>
        "(?:\\s*)"                 // whitespace (optional)
        "(?:=)"                    // '='
        "(?:\\s*)"                 // whitespace (optional)
        "(.*\\S+)"                 // <value>
        "(?:\\s*)"                 // whitespace (optional)
        ,
        boost::regex_constants::optimize);
    std::map<std::string, std::string, boost::beast::iless> map;
    auto const v = beast::rfc2616::split(s.begin(), s.end(), ',');
    for (auto const& kv : v)
    {
        boost::smatch m;
        if (!boost::regex_match(kv, m, kRe1))
            Throw<std::runtime_error>("invalid parameter " + kv);
        auto const result = map.emplace(m[1], m[2]);
        if (!result.second)
            Throw<std::runtime_error>("duplicate parameter " + m[1]);
    }
    return map;
}

//------------------------------------------------------------------------------

#if XRPL_ROCKSDB_AVAILABLE

class import_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testcase(beast::unit_test::AbortT::AbortOnFail) << arg();

        using namespace nudb;
        using namespace nudb::detail;

        pass();
        auto const args = parseArgs(arg());
        bool usage = args.empty();

        if (!usage && args.find("from") == args.end())
        {
            log << "Missing parameter: from";
            usage = true;
        }
        if (!usage && args.find("to") == args.end())
        {
            log << "Missing parameter: to";
            usage = true;
        }
        if (!usage && args.find("buffer") == args.end())
        {
            log << "Missing parameter: buffer";
            usage = true;
        }

        if (usage)
        {
            log << "Usage:\n"
                << "--unittest-arg=from=<from>,to=<to>,buffer=<buffer>\n"
                << "from:   RocksDB database to import from\n"
                << "to:     NuDB database to import to\n"
                << "buffer: Buffer size (bigger is faster)\n"
                << "NuDB database must not already exist.";
            return;
        }

        // This controls the size of the bucket buffer.
        // For a 1TB data file, a 32GB bucket buffer is suggested.
        // The larger the buffer, the faster the import.
        //
        std::size_t const bufferSize = std::stoull(args.at("buffer"));
        auto const fromPath = args.at("from");
        auto const toPath = args.at("to");

        using hash_type = nudb::xxhasher;
        auto const bulkSize = 64 * 1024 * 1024;
        float const loadFactor = 0.5;

        auto const dp = toPath + ".dat";
        auto const kp = toPath + ".key";

        auto const start = std::chrono::steady_clock::now();

        log << "from:    " << fromPath
            << "\n"
               "to:      "
            << toPath
            << "\n"
               "buffer:  "
            << bufferSize;

        std::unique_ptr<rocksdb::DB> db;
        {
            rocksdb::Options options;
            options.create_if_missing = false;
            options.max_open_files = 2000;  // 5000?
            rocksdb::DB* pdb = nullptr;
            rocksdb::Status const status = rocksdb::DB::OpenForReadOnly(options, fromPath, &pdb);
            if (!status.ok() || (pdb == nullptr))
                Throw<std::runtime_error>("Can't open '" + fromPath + "': " + status.ToString());
            db.reset(pdb);
        }
        // Create data file with values
        std::size_t nitems = 0;
        dat_file_header dh{};
        dh.version = currentVersion;
        dh.uid = make_uid();
        dh.appnum = 1;
        dh.key_size = 32;

        native_file df;
        error_code ec;
        df.create(file_mode::append, dp, ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        bulk_writer<native_file> dw(df, 0, bulkSize);
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
            std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(options));

            buffer buf;
            for (it->SeekToFirst(); it->Valid(); it->Next())
            {
                if (it->key().size() != 32)
                {
                    Throw<std::runtime_error>(
                        "Unexpected key size " + std::to_string(it->key().size()));
                }
                void const* const key = it->key().data();
                void const* const data = it->value().data();
                auto const size = it->value().size();
                std::unique_ptr<char[]> const clean(new char[size]);
                std::memcpy(clean.get(), data, size);
                filterInner(clean.get(), size);
                auto const out = nodeobjectCompress(clean.get(), size, buf);
                // Verify codec correctness
                {
                    buffer buf2;
                    auto const check = nodeobjectDecompress(out.first, out.second, buf2);
                    BEAST_EXPECT(check.second == size);
                    BEAST_EXPECT(std::memcmp(check.first, clean.get(), size) == 0);
                }
                // Data Record
                auto os = dw.prepare(
                    field<uint48_t>::size +  // Size
                        32 +                 // Key
                        out.second,
                    ec);
                if (ec)
                    Throw<nudb::system_error>(ec);
                write<uint48_t>(os, out.second);
                std::memcpy(os.data(32), key, 32);
                std::memcpy(os.data(out.second), out.first, out.second);
                ++nitems;
            }
            dw.flush(ec);
            if (ec)
                Throw<nudb::system_error>(ec);
        }
        db.reset();
        log << "Import data: " << detail::fmtdur(std::chrono::steady_clock::now() - start);
        auto const dfSize = df.size(ec);
        if (ec)
            Throw<nudb::system_error>(ec);
        // Create key file
        key_file_header kh{};
        kh.version = currentVersion;
        kh.uid = dh.uid;
        kh.appnum = dh.appnum;
        kh.key_size = 32;
        kh.salt = make_salt();
        kh.pepper = pepper<hash_type>(kh.salt);
        kh.block_size = block_size(kp);
        kh.load_factor = std::min<std::size_t>(65536.0 * loadFactor, 65535);
        kh.buckets = std::ceil(nitems / (bucket_capacity(kh.block_size) * loadFactor));
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
        auto const buckets = std::max<std::size_t>(1, bufferSize / kh.block_size);
        buf.reserve(buckets * kh.block_size);
        auto const passes = (kh.buckets + buckets - 1) / buckets;
        log << "items:   " << nitems
            << "\n"
               "buckets: "
            << kh.buckets
            << "\n"
               "data:    "
            << dfSize
            << "\n"
               "passes:  "
            << passes;
        Progress p(dfSize * passes);
        std::size_t npass = 0;
        for (std::size_t b0 = 0; b0 < kh.buckets; b0 += buckets)
        {
            auto const b1 = std::min(b0 + buckets, kh.buckets);
            // Buffered range is [b0, b1)
            auto const bn = b1 - b0;
            // Create empty buckets
            for (std::size_t i = 0; i < bn; ++i)
            {
                bucket const b(kh.block_size, buf.get() + (i * kh.block_size), empty);
            }
            // Insert all keys into buckets
            // Iterate Data File
            bulk_reader<native_file> r(df, dat_file_header::size, dfSize, bulkSize);
            while (!r.eof())
            {
                auto const offset = r.offset();
                // Data Record or Spill Record
                std::size_t size = 0;
                auto is = r.prepare(field<uint48_t>::size, ec);  // Size
                if (ec)
                    Throw<nudb::system_error>(ec);
                read<uint48_t>(is, size);
                if (size > 0)
                {
                    // Data Record
                    is = r.prepare(
                        dh.key_size +  // Key
                            size,
                        ec);  // Data
                    if (ec)
                        Throw<nudb::system_error>(ec);
                    std::uint8_t const* const key = is.data(dh.key_size);
                    auto const h = hash<hash_type>(key, kh.key_size, kh.salt);
                    auto const n = bucket_index(h, kh.buckets, kh.modulus);
                    p(log, (npass * dfSize) + r.offset());
                    if (n < b0 || n >= b1)
                        continue;
                    bucket b(kh.block_size, buf.get() + ((n - b0) * kh.block_size));
                    maybe_spill(b, dw, ec);
                    if (ec)
                        Throw<nudb::system_error>(ec);
                    b.insert(offset, size, h);
                }
                else
                {
                    // VFALCO Should never get here
                    // Spill Record
                    is = r.prepare(field<std::uint16_t>::size, ec);
                    if (ec)
                        Throw<nudb::system_error>(ec);
                    read<std::uint16_t>(is, size);  // Size
                    r.prepare(size, ec);            // skip
                    if (ec)
                        Throw<nudb::system_error>(ec);
                }
            }
            kf.write((b0 + 1) * kh.block_size, buf.get(), bn * kh.block_size, ec);
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

BEAST_DEFINE_TESTSUITE_MANUAL(import, nodestore, xrpl);

#endif

//------------------------------------------------------------------------------

}  // namespace NodeStore
}  // namespace xrpl
