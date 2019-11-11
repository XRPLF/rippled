//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <ripple/protocol/jss.h>
#include <ripple/core/DatabaseCon.h>

namespace ripple {

class Transaction_test : public beast::unit_test::suite
{

    void
    testRangeRequest()
    {
        testcase("Test Range Request");

        using namespace test::jtx;

        const char* COMMAND   = jss::tx.c_str();
        const char* BINARY    = jss::binary.c_str();
        const char* NOT_FOUND = RPC::get_error_info(rpcTXN_NOT_FOUND).token;
        const char* INVALID   = RPC::get_error_info(rpcINVALID_LGR_RANGE).token;
        const char* EXCESSIVE = RPC::get_error_info(rpcEXCESSIVE_LGR_RANGE).token;

        Env env(*this);
        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();
        
        std::vector<std::shared_ptr<STTx const>> txns;
        auto const startLegSeq = env.current()->info().seq;
        for (int i = 0; i < 750; ++i)
        {
            env(noop(alice));
            txns.emplace_back(env.tx());
            env.close();
        }
        auto const endLegSeq = env.closed()->info().seq;

        // Find the existing transactions
        for (auto&& tx : txns)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(startLegSeq),
                to_string(endLegSeq));

            BEAST_EXPECT(result[jss::result][jss::status] == jss::success);
        }

        auto const tx = env.jt(noop(alice), seq(env.seq(alice))).stx;
        for (int deltaEndSeq = 0; deltaEndSeq < 2; ++deltaEndSeq)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(startLegSeq),
                to_string(endLegSeq + deltaEndSeq));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == NOT_FOUND);

            if (deltaEndSeq)
                BEAST_EXPECT(!result[jss::result][jss::searched_all].asBool());
            else
                BEAST_EXPECT(result[jss::result][jss::searched_all].asBool());
        }

        // Find transactions outside of provided range.
        for (auto&& tx : txns)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(endLegSeq + 1),
                to_string(endLegSeq + 100));

            BEAST_EXPECT(result[jss::result][jss::status] == jss::success);
            BEAST_EXPECT(!result[jss::result][jss::searched_all].asBool());
        }

        const auto deletedLedger = (startLegSeq + endLegSeq) / 2;
        {
            // Remove one of the ledgers from the database directly
            auto db = env.app().getTxnDB().checkoutDb();
            *db << "DELETE FROM Transactions WHERE LedgerSeq == "
                << deletedLedger << ";";
        }

        for (int deltaEndSeq = 0; deltaEndSeq < 2; ++deltaEndSeq)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(startLegSeq),
                to_string(endLegSeq + deltaEndSeq));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == NOT_FOUND);
            BEAST_EXPECT(!result[jss::result][jss::searched_all].asBool());
        }

        // Provide range without providing the `binary`
        // field. (Tests parameter parsing)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                to_string(startLegSeq),
                to_string(endLegSeq));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == NOT_FOUND);

            BEAST_EXPECT(!result[jss::result][jss::searched_all].asBool());
        }

        // Provide range without providing the `binary`
        // field. (Tests parameter parsing)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                to_string(startLegSeq),
                to_string(deletedLedger - 1));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == NOT_FOUND);

            BEAST_EXPECT(result[jss::result][jss::searched_all].asBool());
        }

        // Provide range without providing the `binary`
        // field. (Tests parameter parsing)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(txns[0]->getTransactionID()),
                to_string(startLegSeq),
                to_string(deletedLedger - 1));

            BEAST_EXPECT(result[jss::result][jss::status] == jss::success);
            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (min > max)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(deletedLedger - 1),
                to_string(startLegSeq));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == INVALID);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (min < 0)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(-1),
                to_string(deletedLedger - 1));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == INVALID);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (min < 0, max < 0)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(-20),
                to_string(-10));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == INVALID);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (only one value)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(20));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == INVALID);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (only one value)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                to_string(20));

            // Since we only provided one value for the range,
            // the interface parses it as a false binary flag,
            // since single-value ranges are not accepted.
            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == NOT_FOUND);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (max - min > 1000)
        {
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(startLegSeq),
                to_string(startLegSeq + 1001));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == EXCESSIVE);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

    }

public:
    void run () override
    {
        testRangeRequest();
    }
};

BEAST_DEFINE_TESTSUITE (Transaction, rpc, ripple);

}  // ripple
