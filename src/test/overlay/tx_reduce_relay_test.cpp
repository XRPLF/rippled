//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2020 Ripple Labs Inc.

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
#include <test/jtx/Env.h>

namespace ripple {

namespace test {

class tx_reduce_relay_test : public beast::unit_test::suite
{
    void
    doTest(const std::string& msg, bool log, std::function<void(bool)> f)
    {
        testcase(msg);
        f(log);
    }

    void
    testConfig(bool log)
    {
        doTest("Config Test", log, [&](bool log) {
            auto test = [&](bool enable,
                            bool metrics,
                            std::uint16_t min,
                            std::uint16_t pct,
                            bool success = true) {
                std::stringstream str("[reduce_relay]");
                str << "[reduce_relay]\n"
                    << "tx_enable=" << static_cast<int>(enable) << "\n"
                    << "tx_metrics=" << static_cast<int>(metrics) << "\n"
                    << "tx_min_peers=" << min << "\n"
                    << "tx_relay_percentage=" << pct << "\n";
                Config c;
                try
                {
                    c.loadFromString(str.str());

                    BEAST_EXPECT(c.TX_REDUCE_RELAY_ENABLE == enable);
                    BEAST_EXPECT(c.TX_REDUCE_RELAY_METRICS == metrics);
                    BEAST_EXPECT(c.TX_REDUCE_RELAY_MIN_PEERS == min);
                    BEAST_EXPECT(c.TX_RELAY_PERCENTAGE == pct);
                    if (success)
                        pass();
                    else
                        fail();
                }
                catch (...)
                {
                    if (success)
                        fail();
                    else
                        pass();
                }
            };

            test(true, true, 20, 25);
            test(false, false, 20, 25);
            test(false, false, 20, 0, false);
            test(false, false, 20, 101, false);
            test(false, false, 20, 10, false);
        });
    }

    void
    run() override
    {
        bool log = false;
        testConfig(log);
    }
};

BEAST_DEFINE_TESTSUITE(tx_reduce_relay, ripple_data, ripple);
}  // namespace test
}  // namespace ripple