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

#include <ripple/common/seconds_clock.h>
#include <ripple/types/api/RippleLedgerHash.h>
#include <beast/container/aged_unordered_map.h>
#include <random>
#include <utility>

namespace ripple {
namespace Validators {

class Validators_test : public beast::unit_test::suite
{
public:
    struct Entry
    {
        bool closed = false;    // `true` if the ledger was closed
        bool received = false;  // `true` if we got a validation
    };

    typedef beast::aged_unordered_map <RippleLedgerHash, Entry,
        std::chrono::seconds, beast::hardened_hash<>,
            RippleLedgerHash::key_equal> Table;

    template <class Gen>
    static
    void
    fillrand (void* buffer, std::size_t bytes, Gen& gen)
    {
        auto p = reinterpret_cast<std::uint8_t*>(buffer);
        typedef typename Gen::result_type result_type;
        while (bytes >= sizeof(result_type))
        {
            *reinterpret_cast<result_type*>(p) = gen();
            p += sizeof(result_type);
            bytes -= sizeof(result_type);
        }
        if (bytes > 0)
        {
            auto const v = gen();
            memcpy (p, &v, bytes);
        }
    }

    void
    test_aged_insert()
    {
        testcase ("aged insert");
        std::random_device rng;
        std::mt19937_64 gen {rng()};
        Table table (get_seconds_clock());
        for (int i = 0; i < 10000; ++i)
        {
            std::array <std::uint8_t, RippleLedgerHash::size> buf;
            fillrand (buf.data(), buf.size(), gen);
            RippleLedgerHash h (buf.data(), buf.data() + buf.size());
            auto const result (table.insert (std::make_pair (h, Entry())));
        }
        pass();
    }

    void
    test_Validators()
    {
        int const N (5);
        testcase ("Validators");
        typedef hardened_hash_map <int, Validator> Validators;
        Validators vv;
        for (int i = 0; i < N; ++i)
            vv.emplace (i, Validator{});
        std::random_device rng;
        std::mt19937_64 gen {rng()};
        std::array <std::uint8_t, RippleLedgerHash::size> buf;
        fillrand (buf.data(), buf.size(), gen);
        for (int i = 0; i < 100000; ++i)
        {
            // maybe change the ledger hash
            if ((gen() % 20) == 0)
                fillrand (buf.data(), buf.size(), gen);
            RippleLedgerHash h (buf.data(), buf.data() + buf.size());
            // choose random validator
            Validator& v (vv[gen() % vv.size()]);
            // choose random operation
            //int const choice = gen() % 2;
            int const choice = 1;
            switch (choice)
            {
            case 0:
                v.on_ledger(h);
                break;
            case 1:
                v.on_validation(h);
                break;
            };
        }
        pass();
    }

    void
    run ()
    {
        test_aged_insert();
        test_Validators();
    }
};

BEAST_DEFINE_TESTSUITE(Validators,validators,ripple);

}
}
