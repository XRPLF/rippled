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

#include <ripple/app/rdb/backend/SQLiteDatabase.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/CTID.h>
#include <optional>
#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <tuple>

namespace ripple {

class Transaction_test : public beast::unit_test::suite
{
    std::unique_ptr<Config>
    makeNetworkConfig(uint32_t networkID)
    {
        using namespace test::jtx;
        return envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->NETWORK_ID = networkID;
            return cfg;
        });
    }

    void
    testRangeRequest(FeatureBitset features)
    {
        testcase("Test Range Request");

        using namespace test::jtx;
        using std::to_string;

        const char* COMMAND = jss::tx.c_str();
        const char* BINARY = jss::binary.c_str();
        const char* NOT_FOUND = RPC::get_error_info(rpcTXN_NOT_FOUND).token;
        const char* INVALID = RPC::get_error_info(rpcINVALID_LGR_RANGE).token;
        const char* EXCESSIVE =
            RPC::get_error_info(rpcEXCESSIVE_LGR_RANGE).token;

        Env env{*this, features};
        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();

        std::vector<std::shared_ptr<STTx const>> txns;
        std::vector<std::shared_ptr<STObject const>> metas;
        auto const startLegSeq = env.current()->info().seq;
        for (int i = 0; i < 750; ++i)
        {
            env(noop(alice));
            txns.emplace_back(env.tx());
            env.close();
            metas.emplace_back(
                env.closed()->txRead(env.tx()->getTransactionID()).second);
        }
        auto const endLegSeq = env.closed()->info().seq;

        // Find the existing transactions
        for (size_t i = 0; i < txns.size(); ++i)
        {
            auto const& tx = txns[i];
            auto const& meta = metas[i];
            auto const result = env.rpc(
                COMMAND,
                to_string(tx->getTransactionID()),
                BINARY,
                to_string(startLegSeq),
                to_string(endLegSeq));

            BEAST_EXPECT(result[jss::result][jss::status] == jss::success);
            BEAST_EXPECT(
                result[jss::result][jss::tx] ==
                strHex(tx->getSerializer().getData()));
            BEAST_EXPECT(
                result[jss::result][jss::meta] ==
                strHex(meta->getSerializer().getData()));
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
            dynamic_cast<SQLiteDatabase*>(&env.app().getRelationalDatabase())
                ->deleteTransactionByLedgerSeq(deletedLedger);
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
                COMMAND, to_string(tx->getTransactionID()), to_string(20));

            // Since we only provided one value for the range,
            // the interface parses it as a false binary flag,
            // as single-value ranges are not accepted. Since
            // the error this causes differs depending on the platform
            // we don't call out a specific error here.
            BEAST_EXPECT(result[jss::result][jss::status] == jss::error);

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

    void
    testRangeCTIDRequest(FeatureBitset features)
    {
        testcase("ctid_range");

        using namespace test::jtx;
        using std::to_string;

        const char* COMMAND = jss::tx.c_str();
        const char* BINARY = jss::binary.c_str();
        const char* NOT_FOUND = RPC::get_error_info(rpcTXN_NOT_FOUND).token;
        const char* INVALID = RPC::get_error_info(rpcINVALID_LGR_RANGE).token;
        const char* EXCESSIVE =
            RPC::get_error_info(rpcEXCESSIVE_LGR_RANGE).token;

        Env env{*this, makeNetworkConfig(11111)};
        uint32_t netID = env.app().config().NETWORK_ID;

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();

        std::vector<std::shared_ptr<STTx const>> txns;
        std::vector<std::shared_ptr<STObject const>> metas;
        auto const startLegSeq = env.current()->info().seq;
        for (int i = 0; i < 750; ++i)
        {
            env(noop(alice));
            txns.emplace_back(env.tx());
            env.close();
            metas.emplace_back(
                env.closed()->txRead(env.tx()->getTransactionID()).second);
        }
        auto const endLegSeq = env.closed()->info().seq;

        // Find the existing transactions
        for (size_t i = 0; i < txns.size(); ++i)
        {
            auto const& tx = txns[i];
            auto const& meta = metas[i];
            uint32_t txnIdx = meta->getFieldU32(sfTransactionIndex);
            auto const result = env.rpc(
                COMMAND,
                *RPC::encodeCTID(startLegSeq + i, txnIdx, netID),
                BINARY,
                to_string(startLegSeq),
                to_string(endLegSeq));

            BEAST_EXPECT(result[jss::result][jss::status] == jss::success);
            BEAST_EXPECT(
                result[jss::result][jss::tx] ==
                strHex(tx->getSerializer().getData()));
            BEAST_EXPECT(
                result[jss::result][jss::meta] ==
                strHex(meta->getSerializer().getData()));
        }

        auto const tx = env.jt(noop(alice), seq(env.seq(alice))).stx;
        auto const ctid =
            *RPC::encodeCTID(endLegSeq, tx->getSeqProxy().value(), netID);
        for (int deltaEndSeq = 0; deltaEndSeq < 2; ++deltaEndSeq)
        {
            auto const result = env.rpc(
                COMMAND,
                ctid,
                BINARY,
                to_string(startLegSeq),
                to_string(endLegSeq + deltaEndSeq));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == NOT_FOUND);

            if (deltaEndSeq)
                BEAST_EXPECT(!result[jss::result][jss::searched_all].asBool());
            else
                BEAST_EXPECT(!result[jss::result][jss::searched_all].asBool());
        }

        // Find transactions outside of provided range.
        for (size_t i = 0; i < txns.size(); ++i)
        {
            // auto const& tx = txns[i];
            auto const& meta = metas[i];
            uint32_t txnIdx = meta->getFieldU32(sfTransactionIndex);
            auto const result = env.rpc(
                COMMAND,
                *RPC::encodeCTID(startLegSeq + i, txnIdx, netID),
                BINARY,
                to_string(endLegSeq + 1),
                to_string(endLegSeq + 100));

            BEAST_EXPECT(result[jss::result][jss::status] == jss::success);
            BEAST_EXPECT(!result[jss::result][jss::searched_all].asBool());
        }

        const auto deletedLedger = (startLegSeq + endLegSeq) / 2;
        {
            // Remove one of the ledgers from the database directly
            dynamic_cast<SQLiteDatabase*>(&env.app().getRelationalDatabase())
                ->deleteTransactionByLedgerSeq(deletedLedger);
        }

        for (int deltaEndSeq = 0; deltaEndSeq < 2; ++deltaEndSeq)
        {
            auto const result = env.rpc(
                COMMAND,
                ctid,
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
                COMMAND, ctid, to_string(startLegSeq), to_string(endLegSeq));

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
                ctid,
                to_string(startLegSeq),
                to_string(deletedLedger - 1));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == NOT_FOUND);

            BEAST_EXPECT(!result[jss::result][jss::searched_all].asBool());
        }

        // Provide range without providing the `binary`
        // field. (Tests parameter parsing)
        {
            auto const& meta = metas[0];
            uint32_t txnIdx = meta->getFieldU32(sfTransactionIndex);
            auto const result = env.rpc(
                COMMAND,
                *RPC::encodeCTID(endLegSeq, txnIdx, netID),
                to_string(startLegSeq),
                to_string(deletedLedger - 1));

            BEAST_EXPECT(result[jss::result][jss::status] == jss::success);
            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (min > max)
        {
            auto const result = env.rpc(
                COMMAND,
                ctid,
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
                ctid,
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
            auto const result =
                env.rpc(COMMAND, ctid, BINARY, to_string(-20), to_string(-10));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == INVALID);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (only one value)
        {
            auto const result = env.rpc(COMMAND, ctid, BINARY, to_string(20));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == INVALID);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (only one value)
        {
            auto const result = env.rpc(COMMAND, ctid, to_string(20));

            // Since we only provided one value for the range,
            // the interface parses it as a false binary flag,
            // as single-value ranges are not accepted. Since
            // the error this causes differs depending on the platform
            // we don't call out a specific error here.
            BEAST_EXPECT(result[jss::result][jss::status] == jss::error);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }

        // Provide an invalid range: (max - min > 1000)
        {
            auto const result = env.rpc(
                COMMAND,
                ctid,
                BINARY,
                to_string(startLegSeq),
                to_string(startLegSeq + 1001));

            BEAST_EXPECT(
                result[jss::result][jss::status] == jss::error &&
                result[jss::result][jss::error] == EXCESSIVE);

            BEAST_EXPECT(!result[jss::result].isMember(jss::searched_all));
        }
    }

    void
    testCTIDValidation(FeatureBitset features)
    {
        testcase("ctid_validation");

        using namespace test::jtx;
        using std::to_string;

        Env env{*this, makeNetworkConfig(11111)};

        // Test case 1: Valid input values
        auto const expected11 = std::optional<std::string>("CFFFFFFFFFFFFFFF");
        BEAST_EXPECT(
            RPC::encodeCTID(0x0FFF'FFFFUL, 0xFFFFU, 0xFFFFU) == expected11);
        auto const expected12 = std::optional<std::string>("C000000000000000");
        BEAST_EXPECT(RPC::encodeCTID(0, 0, 0) == expected12);
        auto const expected13 = std::optional<std::string>("C000000100020003");
        BEAST_EXPECT(RPC::encodeCTID(1U, 2U, 3U) == expected13);
        auto const expected14 = std::optional<std::string>("C0CA2AA7326FFFFF");
        BEAST_EXPECT(RPC::encodeCTID(13249191UL, 12911U, 65535U) == expected14);

        // Test case 2: ledger_seq greater than 0xFFFFFFF
        BEAST_EXPECT(!RPC::encodeCTID(0x1000'0000UL, 0xFFFFU, 0xFFFFU));

        // Test case 3: txn_index greater than 0xFFFF
        // this test case is impossible in c++ due to the type, left in for
        // completeness
        auto const expected3 = std::optional<std::string>("CFFFFFFF0000FFFF");
        BEAST_EXPECT(
            RPC::encodeCTID(0x0FFF'FFFF, (uint16_t)0x10000, 0xFFFF) ==
            expected3);

        // Test case 4: network_id greater than 0xFFFF
        // this test case is impossible in c++ due to the type, left in for
        // completeness
        auto const expected4 = std::optional<std::string>("CFFFFFFFFFFF0000");
        BEAST_EXPECT(
            RPC::encodeCTID(0x0FFF'FFFFUL, 0xFFFFU, (uint16_t)0x1000'0U) ==
            expected4);

        // Test case 5: Valid input values
        auto const expected51 =
            std::optional<std::tuple<int32_t, uint16_t, uint16_t>>(
                std::make_tuple(0, 0, 0));
        BEAST_EXPECT(RPC::decodeCTID("C000000000000000") == expected51);
        auto const expected52 =
            std::optional<std::tuple<int32_t, uint16_t, uint16_t>>(
                std::make_tuple(1U, 2U, 3U));
        BEAST_EXPECT(RPC::decodeCTID("C000000100020003") == expected52);
        auto const expected53 =
            std::optional<std::tuple<int32_t, uint16_t, uint16_t>>(
                std::make_tuple(13249191UL, 12911U, 49221U));
        BEAST_EXPECT(RPC::decodeCTID("C0CA2AA7326FC045") == expected53);

        // Test case 6: ctid not a string or big int
        BEAST_EXPECT(!RPC::decodeCTID(0xCFF));

        // Test case 7: ctid not a hexadecimal string
        BEAST_EXPECT(!RPC::decodeCTID("C003FFFFFFFFFFFG"));

        // Test case 8: ctid not exactly 16 nibbles
        BEAST_EXPECT(!RPC::decodeCTID("C003FFFFFFFFFFF"));

        // Test case 9: ctid too large to be a valid CTID value
        BEAST_EXPECT(!RPC::decodeCTID("CFFFFFFFFFFFFFFFF"));

        // Test case 10: ctid doesn't start with a C nibble
        BEAST_EXPECT(!RPC::decodeCTID("FFFFFFFFFFFFFFFF"));

        // Test case 11: Valid input values
        BEAST_EXPECT(
            (RPC::decodeCTID(0xCFFF'FFFF'FFFF'FFFFULL) ==
             std::optional<std::tuple<int32_t, uint16_t, uint16_t>>(
                 std::make_tuple(0x0FFF'FFFFUL, 0xFFFFU, 0xFFFFU))));
        BEAST_EXPECT(
            (RPC::decodeCTID(0xC000'0000'0000'0000ULL) ==
             std::optional<std::tuple<int32_t, uint16_t, uint16_t>>(
                 std::make_tuple(0, 0, 0))));
        BEAST_EXPECT(
            (RPC::decodeCTID(0xC000'0001'0002'0003ULL) ==
             std::optional<std::tuple<int32_t, uint16_t, uint16_t>>(
                 std::make_tuple(1U, 2U, 3U))));
        BEAST_EXPECT(
            (RPC::decodeCTID(0xC0CA'2AA7'326F'C045ULL) ==
             std::optional<std::tuple<int32_t, uint16_t, uint16_t>>(
                 std::make_tuple(1324'9191UL, 12911U, 49221U))));

        // Test case 12: ctid not exactly 16 nibbles
        BEAST_EXPECT(!RPC::decodeCTID(0xC003'FFFF'FFFF'FFF));

        // Test case 13: ctid too large to be a valid CTID value
        // this test case is not possible in c++ because it would overflow the
        // type, left in for completeness
        // BEAST_EXPECT(!RPC::decodeCTID(0xCFFFFFFFFFFFFFFFFULL));

        // Test case 14: ctid doesn't start with a C nibble
        BEAST_EXPECT(!RPC::decodeCTID(0xFFFF'FFFF'FFFF'FFFFULL));
    }

    void
    testCTIDRPC(FeatureBitset features)
    {
        testcase("ctid_rpc");

        using namespace test::jtx;

        // test that the ctid AND the hash are in the response
        {
            Env env{*this, makeNetworkConfig(11111)};
            uint32_t netID = env.app().config().NETWORK_ID;

            auto const alice = Account("alice");
            auto const bob = Account("bob");

            auto const startLegSeq = env.current()->info().seq;
            env.fund(XRP(10000), alice, bob);
            env(pay(alice, bob, XRP(10)));
            env.close();

            auto const ctid = *RPC::encodeCTID(startLegSeq, 0, netID);
            Json::Value jsonTx;
            jsonTx[jss::binary] = false;
            jsonTx[jss::ctid] = ctid;
            jsonTx[jss::id] = 1;
            auto jrr = env.rpc("json", "tx", to_string(jsonTx))[jss::result];
            BEAST_EXPECT(jrr[jss::ctid] == ctid);
            BEAST_EXPECT(jrr[jss::hash]);
        }

        // test that if the network is 65535 the ctid is not in the response
        {
            Env env{*this, makeNetworkConfig(65535)};
            uint32_t netID = env.app().config().NETWORK_ID;

            auto const alice = Account("alice");
            auto const bob = Account("bob");

            auto const startLegSeq = env.current()->info().seq;
            env.fund(XRP(10000), alice, bob);
            env(pay(alice, bob, XRP(10)));
            env.close();

            auto const ctid = *RPC::encodeCTID(startLegSeq, 0, netID);
            Json::Value jsonTx;
            jsonTx[jss::binary] = false;
            jsonTx[jss::ctid] = ctid;
            jsonTx[jss::id] = 1;
            auto jrr = env.rpc("json", "tx", to_string(jsonTx))[jss::result];
            BEAST_EXPECT(!jrr[jss::ctid]);
            BEAST_EXPECT(jrr[jss::hash]);
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        testWithFeats(all);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testRangeRequest(features);
        testRangeCTIDRequest(features);
        testCTIDValidation(features);
        testCTIDRPC(features);
    }
};

BEAST_DEFINE_TESTSUITE(Transaction, rpc, ripple);

}  // namespace ripple
