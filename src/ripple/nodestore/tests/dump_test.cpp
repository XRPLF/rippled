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
#include <ripple/beast/hash/xxhasher.h>
#include <ripple/basics/contract.h>
#include <ripple/nodestore/impl/codec.h>
#include <ripple/nodestore/tests/util.h>
#include <ripple/beast/hash/uhash.h>
#include <ripple/beast/nudb/visit.h>
#include <ripple/beast/nudb/detail/format.h>
#include <ripple/beast/unit_test.h>
#include <beast/http/rfc2616.hpp>
#include <beast/detail/ci_char_traits.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>

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

class dump_test : public beast::unit_test::suite
{
public:
    struct bucket
    {
        std::size_t count = 0; // number of objects
        std::size_t bytes = 0; // total bytes
    };

    using buckets = std::array<bucket, 4>;

    using prefix_t = std::array<char, 3>;

    static
    std::string
    to_string(bucket const& b, std::string const& name)
    {
        return std::to_string(b.count) + " " +
            name + " in " + std::to_string(b.bytes) + " bytes";
    }

    static
    prefix_t
    make_prefix(void const* data)
    {
        auto p = reinterpret_cast<char const*>(data);
        prefix_t prefix;
        prefix[0] = p[0];
        prefix[1] = p[1];
        prefix[2] = p[2];
        return prefix;
    }

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
                "--unittest-arg=path=<path>[,every=<number>]\n" <<
                "path:   NuDB path to database (without the .dat)\n" <<
                "every:  Intermediate report every # items (0 to disable)\n";
            return;
        }

        auto const path = args.at("path");

        std::size_t const every =
            args.find("every") != args.end() ?
                std::stoull(args.at("every")) :
                    1'000'000;

        auto const dp = path + ".dat";
        auto const kp = path + ".key";

        log << "path: " << path << ", every=" << every;

        std::size_t n = 0;
        buckets bs;
        std::unordered_map<prefix_t, bucket, beast::uhash<>> pm;

        auto report =
            [&]
            {
                log << "\n" <<
                    to_string(bs[0], "unknown") << "\n" <<
                    to_string(bs[1], "ledger") << "\n" <<
                    to_string(bs[2], "account") << "\n" <<
                    to_string(bs[3], "tx");
                for(auto const& p : pm)
                    log << std::string(p.first.data(), p.first.size()) <<
                        " " << p.second.count << " items in " <<
                            p.second.bytes << " bytes";
            };

        beast::nudb::visit<nodeobject_codec>(
            dp, 1024 * 1024,
            [&](void const* key, std::size_t key_size,
                void const* data, std::size_t data_size)
            {
                if(data_size < 9)
                    return true;
                {
                    std::uint8_t const* p =
                        reinterpret_cast<std::uint8_t const*>(data);
                    bucket* b;
                    switch(p[8])
                    {
                    default:
                    case hotUNKNOWN:            b = &bs[0]; break;
                    case hotLEDGER:             b = &bs[1]; break;
                    case hotACCOUNT_NODE:       b = &bs[2]; break;
                    case hotTRANSACTION_NODE:   b = &bs[3]; break;
                    }
                    ++b->count;
                    b->bytes += data_size;
                }
                if(data_size >= 11)
                {
                    std::uint8_t const* p =
                        reinterpret_cast<std::uint8_t const*>(data);
                    auto& b = pm[make_prefix(p+9)];
                    ++b.count;
                    b.bytes += data_size;
                }
                ++n;
                if(n > every)
                {
                    report();
                    n = 0;
                }
                return true;
            });
        report();
    }
};

BEAST_DEFINE_TESTSUITE(dump,NodeStore,ripple);

} // NodeStore
} // rippled
