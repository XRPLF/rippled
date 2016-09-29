//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained
#include <nudb/visit.hpp>

#include <nudb/test/test_store.hpp>
#include <nudb/progress.hpp>
#include <beast/unit_test/suite.hpp>
#include <unordered_map>

namespace nudb {
namespace test {

class visit_test : public beast::unit_test::suite
{
public:
    void
    do_visit(
        std::size_t N,
        std::size_t blockSize,
        float loadFactor)
    {
        using key_type = std::uint32_t;

        error_code ec;
        test_store ts{sizeof(key_type), blockSize, loadFactor};

        // File not present
        visit(ts.dp,
            [&](void const* key, std::size_t keySize,
                void const* data, std::size_t dataSize,
                error_code& ec)
            {
            }, no_progress{}, ec);
        if(! BEAST_EXPECTS(ec ==
                errc::no_such_file_or_directory, ec.message()))
            return;
        ec = {};

        ts.create(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ts.open(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        std::unordered_map<key_type, std::size_t> map;
        // Insert
        for(std::size_t i = 0; i < N; ++i)
        {
            auto const item = ts[i];
            key_type const k =         item.key[0]         +
                (static_cast<key_type>(item.key[1]) <<  8) +
                (static_cast<key_type>(item.key[2]) << 16) +
                (static_cast<key_type>(item.key[3]) << 24);
            map[k] = i;
            ts.db.insert(item.key, item.data, item.size, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
        }
        ts.close(ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        // Visit
        visit(ts.dp,
            [&](void const* key, std::size_t keySize,
                void const* data, std::size_t dataSize,
                error_code& ec)
            {
                auto const fail =
                    [&ec]
                    {
                        ec = error_code{
                            errc::invalid_argument, generic_category()};
                    };
                if(! BEAST_EXPECT(keySize == sizeof(key_type)))
                    return fail();
                auto const p =
                    reinterpret_cast<std::uint8_t const*>(key);
                key_type const k =         p[0]         +
                    (static_cast<key_type>(p[1]) <<  8) +
                    (static_cast<key_type>(p[2]) << 16) +
                    (static_cast<key_type>(p[3]) << 24);
                auto const it = map.find(k);
                if(it == map.end())
                    return fail();
                auto const item = ts[it->second];
                if(! BEAST_EXPECT(dataSize == item.size))
                    return fail();
                auto const result =
                    std::memcmp(data, item.data, item.size);
                if(result != 0)
                    return fail();
            }, no_progress{}, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
    }

    void
    run() override
    {
        float const loadFactor = 0.95f;
        do_visit(5000, 4096, loadFactor);
    }
};

BEAST_DEFINE_TESTSUITE(visit, test, nudb);

} // test
} // nudb
