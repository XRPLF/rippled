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

#include <ripple/beast/unit_test.h>
#include <ripple/beast/utility/rngfill.h>
#include <ripple/beast/xor_shift_engine.h>
#include <ripple/protocol/digest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <vector>

namespace ripple {

class digest_test : public beast::unit_test::suite
{
    std::vector<uint256> dataset1;

    template <class Hasher>
    void
    test(char const* name)
    {
        using namespace std::chrono;

        // Prime the cache
        for (int i = 0; i != 4; i++)
        {
            for (auto const& x : dataset1)
            {
                Hasher h;
                h(x.data(), x.size());
                (void)static_cast<typename Hasher::result_type>(h);
            }
        }

        std::array<nanoseconds, 128> results;

        for (auto& result : results)
        {
            auto const start = high_resolution_clock::now();

            for (auto const& x : dataset1)
            {
                Hasher h;
                h(x.data(), x.size());
                (void)static_cast<typename Hasher::result_type>(h);
            }

            auto const d = high_resolution_clock::now() - start;

            result = d;
        }

        log << "    " << name << ":" << '\n';

        auto const sum =
            std::accumulate(results.begin(), results.end(), nanoseconds{0});
        {
            auto s = duration_cast<seconds>(sum);
            auto ms = duration_cast<milliseconds>(sum) - s;
            log << "       Total Time = " << s.count() << "." << ms.count()
                << " seconds" << std::endl;
        }

        auto const mean = sum / results.size();
        {
            auto s = duration_cast<seconds>(mean);
            auto ms = duration_cast<milliseconds>(mean) - s;
            log << "        Mean Time = " << s.count() << "." << ms.count()
                << " seconds" << std::endl;
        }

        std::vector<nanoseconds::rep> diff(results.size());
        std::transform(
            results.begin(),
            results.end(),
            diff.begin(),
            [&mean](nanoseconds trial) { return (trial - mean).count(); });
        auto const sq_sum =
            std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
        {
            nanoseconds const stddev{static_cast<nanoseconds::rep>(
                std::sqrt(sq_sum / results.size()))};
            auto s = duration_cast<seconds>(stddev);
            auto ms = duration_cast<milliseconds>(stddev) - s;
            log << "          Std Dev = " << s.count() << "." << ms.count()
                << " seconds" << std::endl;
        }
    }

public:
    digest_test()
    {
        beast::xor_shift_engine g(19207813);
        std::array<std::uint8_t, 32> buf;

        for (int i = 0; i < 1000000; i++)
        {
            beast::rngfill(buf.data(), buf.size(), g);
            dataset1.push_back(uint256{buf});
        }
    }

    void
    testSHA512()
    {
        testcase("SHA512");
        test<openssl_ripemd160_hasher>("OpenSSL");
        test<beast::ripemd160_hasher>("Beast");
        pass();
    }

    void
    testSHA256()
    {
        testcase("SHA256");
        test<openssl_sha256_hasher>("OpenSSL");
        test<beast::sha256_hasher>("Beast");
        pass();
    }

    void
    testRIPEMD160()
    {
        testcase("RIPEMD160");
        test<openssl_ripemd160_hasher>("OpenSSL");
        test<beast::ripemd160_hasher>("Beast");
        pass();
    }

    void
    run() override
    {
        testSHA512();
        testSHA256();
        testRIPEMD160();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(digest, ripple_data, ripple, 20);

}  // namespace ripple
