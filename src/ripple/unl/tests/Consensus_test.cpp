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
#include <ripple/unl/tests/metrics.h>
#include <ripple/unl/tests/Sim1.h>
#include <ripple/unl/tests/Sim2.h>
#include <ripple/unl/tests/Sim3.h>
#include <ripple/unl/tests/Sim4.h>
#include <beast/unit_test/suite.h>

namespace ripple {
namespace test {

class Consensus_test : public beast::unit_test::suite
{
public:
    void
    run()
    {
        Sim4<log_t>::run(log);
        //Sim3<log_t>::run(log);
        //Sim2<log_t>::run(log);
        //Sim1::run(log);
        pass();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Consensus,sim,ripple);

}
}
