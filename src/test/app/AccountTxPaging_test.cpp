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
#include <ripple/core/DatabaseCon.h>
#include <ripple/app/misc/impl/AccountTxPaging.h>
#include <ripple/protocol/types.h>
#include <test/support/jtx.h>
#include <ripple/beast/unit_test.h>
#include <cstdlib>
#include <memory>
#include <vector>

namespace ripple {

struct AccountTxPaging_test : beast::unit_test::suite
{
    std::unique_ptr<DatabaseCon> db_;
    std::unique_ptr<AccountIDCache> idCache_;
    NetworkOPs::AccountTxs txs_;
    AccountID account_;

    void
    run() override
    {
        std::string data_path;

        if (auto const fixtures = std::getenv("TEST_FIXTURES"))
            data_path = fixtures;

        if (data_path.empty ())
        {
            fail("The 'TEST_FIXTURES' environment variable is empty.");
            return;
        }

        DatabaseCon::Setup dbConf;
        dbConf.dataDir = data_path + "/";

        db_ = std::make_unique <DatabaseCon> (
            dbConf, "account-tx-transactions.db", nullptr, 0);

        idCache_ = std::make_unique<AccountIDCache>(128000);

        account_ = *parseBase58<AccountID>(
            "rfu6L5p3azwPzQZsbTafuVk884N9YoKvVG");

        testAccountTxPaging();
    }

    void
    checkToken (Json::Value const& token, int ledger, int sequence)
    {
        BEAST_EXPECT(token.isMember ("ledger"));
        BEAST_EXPECT(token["ledger"].asInt() == ledger);
        BEAST_EXPECT(token.isMember ("seq"));
        BEAST_EXPECT(token["seq"].asInt () == sequence);
    }

    void
    checkTransaction (NetworkOPs::AccountTx const& tx, int ledger, int index)
    {
        BEAST_EXPECT(tx.second->getLgrSeq () == ledger);
        BEAST_EXPECT(tx.second->getIndex () == index);
    }

    std::size_t
    next (
        int limit,
        bool forward,
        Json::Value& token,
        std::int32_t minLedger,
        std::int32_t maxLedger)
    {
        txs_.clear();

        std::int32_t const page_length = 200;
        bool const admin = true;

        test::jtx::Env env(*this);
        Application& app = env.app();
        auto& txs = txs_;

        auto bound = [&txs, &app](
            std::uint32_t ledger_index,
            std::string const& status,
            Blob const& rawTxn,
            Blob const& rawMeta)
        {
            convertBlobsToTxResult (
                txs, ledger_index, status, rawTxn, rawMeta, app);
        };

        accountTxPage(*db_, *idCache_, [](std::uint32_t){}, bound, account_, minLedger,
            maxLedger, forward, token, limit, admin, page_length);

        return txs_.size();
    }

    void
    testAccountTxPaging ()
    {
        using namespace std::placeholders;

        bool const forward = true;

        std::int32_t min_ledger;
        std::int32_t max_ledger;
        Json::Value token;
        int limit;

        // the supplied account-tx-transactions.db contains contains
        // transactions with the following ledger/sequence pairs.
        //  3|5
        //  4|4
        //  4|10
        //  5|4
        //  5|7
        //  6|1
        //  6|5
        //  6|6
        //  6|7
        //  6|8
        //  6|9
        //  6|10
        //  6|11

        min_ledger = 2;
        max_ledger = 5;

        {
            limit = 2;

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 2);
            checkTransaction (txs_[0], 3, 5);
            checkTransaction (txs_[1], 4, 4);
            checkToken (token, 4, 10);

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 2);
            checkTransaction (txs_[0], 4, 10);
            checkTransaction (txs_[1], 5, 4);
            checkToken (token, 5, 7);

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 1);
            checkTransaction (txs_[0], 5, 7);

            BEAST_EXPECT(! token["ledger"]);
            BEAST_EXPECT(! token["seq"]);
        }

        token = Json::nullValue;

        min_ledger = 3;
        max_ledger = 9;

        {
            limit = 1;

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 1);
            checkTransaction (txs_[0], 3, 5);
            checkToken (token, 4, 4);

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 1);
            checkTransaction (txs_[0], 4, 4);
            checkToken (token, 4, 10);

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 1);
            checkTransaction (txs_[0], 4, 10);
            checkToken (token, 5, 4);
        }

        {
            limit = 3;

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 3);
            checkTransaction (txs_[0], 5, 4);
            checkTransaction (txs_[1], 5, 7);
            checkTransaction (txs_[2], 6, 1);
            checkToken (token, 6, 5);

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 3);
            checkTransaction (txs_[0], 6, 5);
            checkTransaction (txs_[1], 6, 6);
            checkTransaction (txs_[2], 6, 7);
            checkToken (token, 6, 8);

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 3);
            checkTransaction (txs_[0], 6, 8);
            checkTransaction (txs_[1], 6, 9);
            checkTransaction (txs_[2], 6, 10);
            checkToken (token, 6, 11);

            BEAST_EXPECT(next(limit, forward, token, min_ledger, max_ledger) == 1);
            checkTransaction (txs_[0], 6, 11);

            BEAST_EXPECT(! token["ledger"]);
            BEAST_EXPECT(! token["seq"]);
        }

        token = Json::nullValue;

        {
            limit = 2;

            BEAST_EXPECT(next(limit, ! forward, token, min_ledger, max_ledger) == 2);
            checkTransaction (txs_[0], 6, 11);
            checkTransaction (txs_[1], 6, 10);
            checkToken (token, 6, 9);

            BEAST_EXPECT(next(limit, ! forward, token, min_ledger, max_ledger) == 2);
            checkTransaction (txs_[0], 6, 9);
            checkTransaction (txs_[1], 6, 8);
            checkToken (token, 6, 7);
        }

        {
            limit = 3;

            BEAST_EXPECT(next(limit, ! forward, token, min_ledger, max_ledger) == 3);
            checkTransaction (txs_[0], 6, 7);
            checkTransaction (txs_[1], 6, 6);
            checkTransaction (txs_[2], 6, 5);
            checkToken (token, 6, 1);

            BEAST_EXPECT(next(limit, ! forward, token, min_ledger, max_ledger) == 3);
            checkTransaction (txs_[0], 6, 1);
            checkTransaction (txs_[1], 5, 7);
            checkTransaction (txs_[2], 5, 4);
            checkToken (token, 4, 10);

            BEAST_EXPECT(next(limit, ! forward, token, min_ledger, max_ledger) == 3);
            checkTransaction (txs_[0], 4, 10);
            checkTransaction (txs_[1], 4, 4);
            checkTransaction (txs_[2], 3, 5);
        }

        BEAST_EXPECT(! token["ledger"]);
        BEAST_EXPECT(! token["seq"]);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(AccountTxPaging,app,ripple);

}
