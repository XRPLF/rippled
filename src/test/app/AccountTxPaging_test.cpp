//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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
#include <ripple/protocol/SField.h>
#include <ripple/protocol/jss.h>
#include <cstdlib>
#include <test/jtx.h>

#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/rpc/GRPCTestClientBase.h>

namespace ripple {

class AccountTxPaging_test : public beast::unit_test::suite
{
    bool
    checkTransaction(Json::Value const& tx, int sequence, int ledger)
    {
        return (
            tx[jss::tx][jss::Sequence].asInt() == sequence &&
            tx[jss::tx][jss::ledger_index].asInt() == ledger);
    }

    auto
    next(
        test::jtx::Env& env,
        test::jtx::Account const& account,
        int ledger_min,
        int ledger_max,
        int limit,
        bool forward,
        Json::Value const& marker = Json::nullValue)
    {
        Json::Value jvc;
        jvc[jss::account] = account.human();
        jvc[jss::ledger_index_min] = ledger_min;
        jvc[jss::ledger_index_max] = ledger_max;
        jvc[jss::forward] = forward;
        jvc[jss::limit] = limit;
        if (marker)
            jvc[jss::marker] = marker;

        return env.rpc("json", "account_tx", to_string(jvc))[jss::result];
    }

    void
    testAccountTxPaging()
    {
        testcase("Paging for Single Account");
        using namespace test::jtx;

        Env env(*this);
        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};

        env.fund(XRP(10000), A1, A2, A3);
        env.close();

        env.trust(A3["USD"](1000), A1);
        env.trust(A2["USD"](1000), A1);
        env.trust(A3["USD"](1000), A2);
        env.close();

        for (auto i = 0; i < 5; ++i)
        {
            env(pay(A2, A1, A2["USD"](2)));
            env(pay(A3, A1, A3["USD"](2)));
            env(offer(A1, XRP(11), A1["USD"](1)));
            env(offer(A2, XRP(10), A2["USD"](1)));
            env(offer(A3, XRP(9), A3["USD"](1)));
            env.close();
        }

        /* The sequence/ledger for A3 are as follows:
         * seq     ledger_index
         * 3  ----> 3
         * 1  ----> 3
         * 2  ----> 4
         * 2  ----> 4
         * 2  ----> 5
         * 3  ----> 5
         * 4  ----> 6
         * 5  ----> 6
         * 6  ----> 7
         * 7  ----> 7
         * 8  ----> 8
         * 9  ----> 8
         * 10 ----> 9
         * 11 ----> 9
         */

        // page through the results in several ways.
        {
            // limit = 2, 3 batches giving the first 6 txs
            auto jrr = next(env, A3, 2, 5, 2, true);
            auto txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            BEAST_EXPECT(checkTransaction(txs[1u], 3, 3));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 2, 5, 2, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[1u], 4, 4));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 2, 5, 2, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 5));
            BEAST_EXPECT(checkTransaction(txs[1u], 5, 5));
            BEAST_EXPECT(!jrr[jss::marker]);
        }

        {
            // limit 1, 3 requests giving the first 3 txs
            auto jrr = next(env, A3, 3, 9, 1, true);
            auto txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 1, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 1, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            // continue with limit 3, to end of all txs
            jrr = next(env, A3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[1u], 4, 5));
            BEAST_EXPECT(checkTransaction(txs[2u], 5, 5));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 6, 6));
            BEAST_EXPECT(checkTransaction(txs[1u], 7, 6));
            BEAST_EXPECT(checkTransaction(txs[2u], 8, 7));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 9, 7));
            BEAST_EXPECT(checkTransaction(txs[1u], 10, 8));
            BEAST_EXPECT(checkTransaction(txs[2u], 11, 8));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 12, 9));
            BEAST_EXPECT(checkTransaction(txs[1u], 13, 9));
            BEAST_EXPECT(!jrr[jss::marker]);
        }

        {
            // limit 2, descending, 2 batches giving last 4 txs
            auto jrr = next(env, A3, 3, 9, 2, false);
            auto txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 13, 9));
            BEAST_EXPECT(checkTransaction(txs[1u], 12, 9));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 2, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 11, 8));
            BEAST_EXPECT(checkTransaction(txs[1u], 10, 8));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            // continue with limit 3 until all txs have been seen
            jrr = next(env, A3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 9, 7));
            BEAST_EXPECT(checkTransaction(txs[1u], 8, 7));
            BEAST_EXPECT(checkTransaction(txs[2u], 7, 6));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 6, 6));
            BEAST_EXPECT(checkTransaction(txs[1u], 5, 5));
            BEAST_EXPECT(checkTransaction(txs[2u], 4, 5));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[1u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[2u], 3, 3));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            BEAST_EXPECT(!jrr[jss::marker]);
        }
    }

    class GrpcAccountTxClient : public test::GRPCTestClientBase
    {
    public:
        org::xrpl::rpc::v1::GetAccountTransactionHistoryRequest request;
        org::xrpl::rpc::v1::GetAccountTransactionHistoryResponse reply;

        explicit GrpcAccountTxClient(std::string const& port)
            : GRPCTestClientBase(port)
        {
        }

        void
        AccountTx()
        {
            status =
                stub_->GetAccountTransactionHistory(&context, request, &reply);
        }
    };

    bool
    checkTransaction(
        org::xrpl::rpc::v1::GetTransactionResponse const& tx,
        int sequence,
        int ledger)
    {
        return (
            tx.transaction().sequence().value() == sequence &&
            tx.ledger_index() == ledger);
    }

    std::pair<
        org::xrpl::rpc::v1::GetAccountTransactionHistoryResponse,
        grpc::Status>
    nextBinary(
        std::string grpcPort,
        test::jtx::Env& env,
        std::string const& account = "",
        int ledger_min = -1,
        int ledger_max = -1,
        int limit = -1,
        bool forward = false,
        org::xrpl::rpc::v1::Marker* marker = nullptr)
    {
        GrpcAccountTxClient client{grpcPort};
        auto& request = client.request;
        if (account != "")
            request.mutable_account()->set_address(account);
        if (ledger_min != -1)
            request.mutable_ledger_range()->set_ledger_index_min(ledger_min);
        if (ledger_max != -1)
            request.mutable_ledger_range()->set_ledger_index_max(ledger_max);
        request.set_forward(forward);
        request.set_binary(true);
        if (limit != -1)
            request.set_limit(limit);
        if (marker)
        {
            *request.mutable_marker() = *marker;
        }

        client.AccountTx();
        return {client.reply, client.status};
    }

    std::pair<
        org::xrpl::rpc::v1::GetAccountTransactionHistoryResponse,
        grpc::Status>
    next(
        std::string grpcPort,
        test::jtx::Env& env,
        std::string const& account = "",
        int ledger_min = -1,
        int ledger_max = -1,
        int limit = -1,
        bool forward = false,
        org::xrpl::rpc::v1::Marker* marker = nullptr)
    {
        GrpcAccountTxClient client{grpcPort};
        auto& request = client.request;
        if (account != "")
            request.mutable_account()->set_address(account);
        if (ledger_min != -1)
            request.mutable_ledger_range()->set_ledger_index_min(ledger_min);
        if (ledger_max != -1)
            request.mutable_ledger_range()->set_ledger_index_max(ledger_max);
        request.set_forward(forward);
        if (limit != -1)
            request.set_limit(limit);
        if (marker)
        {
            *request.mutable_marker() = *marker;
        }

        client.AccountTx();
        return {client.reply, client.status};
    }

    std::pair<
        org::xrpl::rpc::v1::GetAccountTransactionHistoryResponse,
        grpc::Status>
    nextWithSeq(
        std::string grpcPort,
        test::jtx::Env& env,
        std::string const& account = "",
        int ledger_seq = -1,
        int limit = -1,
        bool forward = false,
        org::xrpl::rpc::v1::Marker* marker = nullptr)
    {
        GrpcAccountTxClient client{grpcPort};
        auto& request = client.request;
        if (account != "")
            request.mutable_account()->set_address(account);
        if (ledger_seq != -1)
            request.mutable_ledger_specifier()->set_sequence(ledger_seq);
        request.set_forward(forward);
        if (limit != -1)
            request.set_limit(limit);
        if (marker)
        {
            *request.mutable_marker() = *marker;
        }

        client.AccountTx();
        return {client.reply, client.status};
    }

    std::pair<
        org::xrpl::rpc::v1::GetAccountTransactionHistoryResponse,
        grpc::Status>
    nextWithHash(
        std::string grpcPort,
        test::jtx::Env& env,
        std::string const& account = "",
        uint256 const& hash = beast::zero,
        int limit = -1,
        bool forward = false,
        org::xrpl::rpc::v1::Marker* marker = nullptr)
    {
        GrpcAccountTxClient client{grpcPort};
        auto& request = client.request;
        if (account != "")
            request.mutable_account()->set_address(account);
        if (hash != beast::zero)
            request.mutable_ledger_specifier()->set_hash(
                hash.data(), hash.size());
        request.set_forward(forward);
        if (limit != -1)
            request.set_limit(limit);
        if (marker)
        {
            *request.mutable_marker() = *marker;
        }

        client.AccountTx();
        return {client.reply, client.status};
    }

    void
    testAccountTxParametersGrpc()
    {
        testcase("Test Account_tx Grpc");

        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));

        Account A1{"A1"};
        env.fund(XRP(10000), A1);
        env.close();

        // Ledger 3 has the two txs associated with funding the account
        // All other ledgers have no txs

        auto hasTxs = [](auto res) {
            return res.second.error_code() == 0 &&
                (res.first.transactions().size() == 2) &&
                //(res.transactions()[0u].transaction().has_account_set()) &&
                (res.first.transactions()[1u].transaction().has_payment());
        };
        auto noTxs = [](auto res) {
            return res.second.error_code() == 0 &&
                (res.first.transactions().size() == 0);
        };

        auto isErr = [](auto res, auto expect) {
            return res.second.error_code() == expect;
        };

        BEAST_EXPECT(
            isErr(next(grpcPort, env, ""), grpc::StatusCode::INVALID_ARGUMENT));

        BEAST_EXPECT(isErr(
            next(grpcPort, env, "0xDEADBEEF"),
            grpc::StatusCode::INVALID_ARGUMENT));

        BEAST_EXPECT(hasTxs(next(grpcPort, env, A1.human())));

        // Ledger min/max index
        {
            BEAST_EXPECT(hasTxs(next(grpcPort, env, A1.human())));

            BEAST_EXPECT(hasTxs(next(grpcPort, env, A1.human(), 0, 100)));

            BEAST_EXPECT(noTxs(next(grpcPort, env, A1.human(), 1, 2)));

            BEAST_EXPECT(isErr(
                next(grpcPort, env, A1.human(), 2, 1),
                grpc::StatusCode::INVALID_ARGUMENT));
        }

        // Ledger index min only
        {
            BEAST_EXPECT(hasTxs(next(grpcPort, env, A1.human(), -1)));

            BEAST_EXPECT(hasTxs(next(grpcPort, env, A1.human(), 1)));

            BEAST_EXPECT(isErr(
                next(grpcPort, env, A1.human(), env.current()->info().seq),
                grpc::StatusCode::INVALID_ARGUMENT));
        }

        // Ledger index max only
        {
            BEAST_EXPECT(hasTxs(next(grpcPort, env, A1.human(), -1, -1)));

            BEAST_EXPECT(hasTxs(next(
                grpcPort, env, A1.human(), -1, env.current()->info().seq)));

            BEAST_EXPECT(hasTxs(
                next(grpcPort, env, A1.human(), -1, env.closed()->info().seq)));

            BEAST_EXPECT(noTxs(next(
                grpcPort, env, A1.human(), -1, env.closed()->info().seq - 1)));
        }
        // Ledger Sequence
        {
            BEAST_EXPECT(hasTxs(nextWithSeq(
                grpcPort, env, A1.human(), env.closed()->info().seq)));

            BEAST_EXPECT(noTxs(nextWithSeq(
                grpcPort, env, A1.human(), env.closed()->info().seq - 1)));

            BEAST_EXPECT(isErr(
                nextWithSeq(
                    grpcPort, env, A1.human(), env.current()->info().seq),
                grpc::StatusCode::INVALID_ARGUMENT));

            BEAST_EXPECT(isErr(
                nextWithSeq(
                    grpcPort, env, A1.human(), env.current()->info().seq + 1),
                grpc::StatusCode::NOT_FOUND));
        }

        // Ledger Hash
        {
            BEAST_EXPECT(hasTxs(nextWithHash(
                grpcPort, env, A1.human(), env.closed()->info().hash)));

            BEAST_EXPECT(noTxs(nextWithHash(
                grpcPort, env, A1.human(), env.closed()->info().parentHash)));
        }
    }

    struct TxCheck
    {
        uint32_t sequence;
        uint32_t ledgerIndex;
        std::string hash;
        std::function<bool(org::xrpl::rpc::v1::Transaction const& res)>
            checkTxn;
    };

    void
    testAccountTxContentsGrpc()
    {
        testcase("Test AccountTx context grpc");
        // Get results for all transaction types that can be associated
        // with an account.  Start by generating all transaction types.
        using namespace test::jtx;
        using namespace std::chrono_literals;

        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));
        // Set time to this value (or greater) to get delivered_amount in meta
        env.timeKeeper().set(NetClock::time_point{446000001s});
        Account const alice{"alice"};
        Account const alie{"alie"};
        Account const gw{"gw"};
        auto const USD{gw["USD"]};

        std::vector<std::shared_ptr<STTx const>> txns;

        env.fund(XRP(1000000), alice, gw);
        env.close();

        // AccountSet
        env(noop(alice));

        txns.emplace_back(env.tx());
        // Payment
        env(pay(alice, gw, XRP(100)), stag(42), dtag(24), last_ledger_seq(20));

        txns.emplace_back(env.tx());
        // Regular key set
        env(regkey(alice, alie));
        env.close();

        txns.emplace_back(env.tx());
        // Trust and Offers
        env(trust(alice, USD(200)), sig(alie));

        txns.emplace_back(env.tx());
        std::uint32_t const offerSeq{env.seq(alice)};
        env(offer(alice, USD(50), XRP(150)), sig(alie));

        txns.emplace_back(env.tx());
        env.close();

        {
            Json::Value cancelOffer;
            cancelOffer[jss::Account] = alice.human();
            cancelOffer[jss::OfferSequence] = offerSeq;
            cancelOffer[jss::TransactionType] = jss::OfferCancel;
            env(cancelOffer, sig(alie));
        }
        env.close();

        txns.emplace_back(env.tx());

        // SignerListSet
        env(signers(alice, 1, {{"bogie", 1}, {"demon", 1}, {gw, 1}}),
            sig(alie));

        txns.emplace_back(env.tx());
        // Escrow
        {
            // Create an escrow.  Requires either a CancelAfter or FinishAfter.
            auto escrow = [](Account const& account,
                             Account const& to,
                             STAmount const& amount) {
                Json::Value escro;
                escro[jss::TransactionType] = jss::EscrowCreate;
                escro[jss::Flags] = tfUniversal;
                escro[jss::Account] = account.human();
                escro[jss::Destination] = to.human();
                escro[jss::Amount] = amount.getJson(JsonOptions::none);
                return escro;
            };

            NetClock::time_point const nextTime{env.now() + 2s};

            Json::Value escrowWithFinish{escrow(alice, alice, XRP(500))};
            escrowWithFinish[sfFinishAfter.jsonName] =
                nextTime.time_since_epoch().count();

            std::uint32_t const escrowFinishSeq{env.seq(alice)};
            env(escrowWithFinish, sig(alie));

            txns.emplace_back(env.tx());
            Json::Value escrowWithCancel{escrow(alice, alice, XRP(500))};
            escrowWithCancel[sfFinishAfter.jsonName] =
                nextTime.time_since_epoch().count();
            escrowWithCancel[sfCancelAfter.jsonName] =
                nextTime.time_since_epoch().count() + 1;

            std::uint32_t const escrowCancelSeq{env.seq(alice)};
            env(escrowWithCancel, sig(alie));
            env.close();

            txns.emplace_back(env.tx());
            {
                Json::Value escrowFinish;
                escrowFinish[jss::TransactionType] = jss::EscrowFinish;
                escrowFinish[jss::Flags] = tfUniversal;
                escrowFinish[jss::Account] = alice.human();
                escrowFinish[sfOwner.jsonName] = alice.human();
                escrowFinish[sfOfferSequence.jsonName] = escrowFinishSeq;
                env(escrowFinish, sig(alie));

                txns.emplace_back(env.tx());
            }
            {
                Json::Value escrowCancel;
                escrowCancel[jss::TransactionType] = jss::EscrowCancel;
                escrowCancel[jss::Flags] = tfUniversal;
                escrowCancel[jss::Account] = alice.human();
                escrowCancel[sfOwner.jsonName] = alice.human();
                escrowCancel[sfOfferSequence.jsonName] = escrowCancelSeq;
                env(escrowCancel, sig(alie));

                txns.emplace_back(env.tx());
            }
            env.close();
        }

        // PayChan
        {
            std::uint32_t payChanSeq{env.seq(alice)};
            Json::Value payChanCreate;
            payChanCreate[jss::TransactionType] = jss::PaymentChannelCreate;
            payChanCreate[jss::Flags] = tfUniversal;
            payChanCreate[jss::Account] = alice.human();
            payChanCreate[jss::Destination] = gw.human();
            payChanCreate[jss::Amount] =
                XRP(500).value().getJson(JsonOptions::none);
            payChanCreate[sfSettleDelay.jsonName] =
                NetClock::duration{100s}.count();
            payChanCreate[sfPublicKey.jsonName] = strHex(alice.pk().slice());
            env(payChanCreate, sig(alie));
            env.close();

            txns.emplace_back(env.tx());
            std::string const payChanIndex{
                strHex(keylet::payChan(alice, gw, payChanSeq).key)};

            {
                Json::Value payChanFund;
                payChanFund[jss::TransactionType] = jss::PaymentChannelFund;
                payChanFund[jss::Flags] = tfUniversal;
                payChanFund[jss::Account] = alice.human();
                payChanFund[sfPayChannel.jsonName] = payChanIndex;
                payChanFund[jss::Amount] =
                    XRP(200).value().getJson(JsonOptions::none);
                env(payChanFund, sig(alie));
                env.close();

                txns.emplace_back(env.tx());
            }
            {
                Json::Value payChanClaim;
                payChanClaim[jss::TransactionType] = jss::PaymentChannelClaim;
                payChanClaim[jss::Flags] = tfClose;
                payChanClaim[jss::Account] = gw.human();
                payChanClaim[sfPayChannel.jsonName] = payChanIndex;
                payChanClaim[sfPublicKey.jsonName] = strHex(alice.pk().slice());
                env(payChanClaim);
                env.close();

                txns.emplace_back(env.tx());
            }
        }

        // Check
        {
            uint256 const aliceCheckId{getCheckIndex(alice, env.seq(alice))};
            env(check::create(alice, gw, XRP(300)), sig(alie));

            auto txn = env.tx();
            uint256 const gwCheckId{getCheckIndex(gw, env.seq(gw))};
            env(check::create(gw, alice, XRP(200)));
            env.close();

            // need to switch the order of the previous 2 txns, since they are
            // in the same ledger and account_tx returns them in a different
            // order
            txns.emplace_back(env.tx());
            txns.emplace_back(txn);
            env(check::cash(alice, gwCheckId, XRP(200)), sig(alie));

            txns.emplace_back(env.tx());
            env(check::cancel(alice, aliceCheckId), sig(alie));

            txns.emplace_back(env.tx());
            env.close();
        }

        // Deposit preauthorization.
        env(deposit::auth(alice, gw), sig(alie));
        env.close();

        txns.emplace_back(env.tx());
        // Multi Sig with memo
        auto const baseFee = env.current()->fees().base;
        env(noop(alice),
            msig(gw),
            fee(2 * baseFee),
            memo("data", "format", "type"));
        env.close();

        txns.emplace_back(env.tx());
        if (!BEAST_EXPECT(txns.size() == 20))
            return;
        // Setup is done.  Look at the transactions returned by account_tx.

        static const TxCheck txCheck[]{
            {21,
             15,
             strHex(txns[txns.size() - 1]->getTransactionID()),
             [this, &txns](auto res) {
                 auto txnJson =
                     txns[txns.size() - 1]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_account_set()) &&
                     BEAST_EXPECT(res.has_fee()) &&
                     BEAST_EXPECT(res.fee().drops() == 20) &&
                     BEAST_EXPECT(res.memos_size() == 1) &&
                     BEAST_EXPECT(res.memos(0).has_memo_data()) &&
                     BEAST_EXPECT(res.memos(0).memo_data().value() == "data") &&
                     BEAST_EXPECT(res.memos(0).has_memo_format()) &&
                     BEAST_EXPECT(
                            res.memos(0).memo_format().value() == "format") &&
                     BEAST_EXPECT(res.memos(0).has_memo_type()) &&
                     BEAST_EXPECT(res.memos(0).memo_type().value() == "type") &&
                     BEAST_EXPECT(res.has_signing_public_key()) &&
                     BEAST_EXPECT(res.signing_public_key().value() == "") &&
                     BEAST_EXPECT(res.signers_size() == 1) &&
                     BEAST_EXPECT(res.signers(0).has_account()) &&
                     BEAST_EXPECT(
                            res.signers(0).account().value().address() ==
                            txnJson["Signers"][0u]["Signer"]["Account"]) &&
                     BEAST_EXPECT(res.signers(0).has_transaction_signature()) &&
                     BEAST_EXPECT(
                            strHex(res.signers(0)
                                       .transaction_signature()
                                       .value()) ==
                            txnJson["Signers"][0u]["Signer"]["TxnSignature"]) &&
                     BEAST_EXPECT(res.signers(0).has_signing_public_key()) &&
                     BEAST_EXPECT(
                            strHex(
                                res.signers(0).signing_public_key().value()) ==
                            txnJson["Signers"][0u]["Signer"]["SigningPubKey"]);
             }},
            {20,
             14,
             strHex(txns[txns.size() - 2]->getTransactionID()),
             [&txns, this](auto res) {
                 return BEAST_EXPECT(res.has_deposit_preauth()) &&
                     BEAST_EXPECT(
                            res.deposit_preauth()
                                .authorize()
                                .value()
                                .address() ==
                            // TODO do them all like this
                            txns[txns.size() - 2]->getJson(
                                JsonOptions::none)["Authorize"]);
             }},
            {19,
             13,
             strHex(txns[txns.size() - 3]->getTransactionID()),
             [&txns, this](auto res) {
                 return BEAST_EXPECT(res.has_check_cancel()) &&
                     BEAST_EXPECT(
                            strHex(res.check_cancel().check_id().value()) ==

                            txns[txns.size() - 3]->getJson(
                                JsonOptions::none)["CheckID"]);
             }},
            {18,
             13,
             strHex(txns[txns.size() - 4]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 4]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_check_cash()) &&
                     BEAST_EXPECT(
                            strHex(res.check_cash().check_id().value()) ==
                            txnJson["CheckID"]) &&
                     BEAST_EXPECT(res.check_cash()
                                      .amount()
                                      .value()
                                      .has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.check_cash()
                                .amount()
                                .value()
                                .xrp_amount()
                                .drops() == txnJson["Amount"].asUInt());
             }},
            {17,
             12,
             strHex(txns[txns.size() - 5]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 5]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_check_create()) &&
                     BEAST_EXPECT(
                            res.check_create()
                                .destination()
                                .value()
                                .address() == txnJson["Destination"]) &&
                     BEAST_EXPECT(res.check_create()
                                      .send_max()
                                      .value()
                                      .has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.check_create()
                                .send_max()
                                .value()
                                .xrp_amount()
                                .drops() == txnJson["SendMax"].asUInt());
             }},
            {5,
             12,
             strHex(txns[txns.size() - 6]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 6]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_check_create()) &&
                     BEAST_EXPECT(
                            res.check_create()
                                .destination()
                                .value()
                                .address() == txnJson["Destination"]) &&
                     BEAST_EXPECT(res.check_create()
                                      .send_max()
                                      .value()
                                      .has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.check_create()
                                .send_max()
                                .value()
                                .xrp_amount()
                                .drops() ==

                            txnJson["SendMax"].asUInt());
             }},
            {4,
             11,
             strHex(txns[txns.size() - 7]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 7]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_payment_channel_claim()) &&
                     BEAST_EXPECT(
                            strHex(res.payment_channel_claim()
                                       .channel()
                                       .value()) == txnJson["Channel"]) &&
                     BEAST_EXPECT(
                            strHex(res.payment_channel_claim()
                                       .public_key()
                                       .value()) == txnJson["PublicKey"]);
             }},
            {16,
             10,
             strHex(txns[txns.size() - 8]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 8]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_payment_channel_fund()) &&
                     BEAST_EXPECT(
                            strHex(
                                res.payment_channel_fund().channel().value()) ==
                            txnJson["Channel"]) &&
                     BEAST_EXPECT(res.payment_channel_fund()
                                      .amount()
                                      .value()
                                      .has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.payment_channel_fund()
                                .amount()
                                .value()
                                .xrp_amount()
                                .drops() == txnJson["Amount"].asUInt());
             }},
            {15,
             9,
             strHex(txns[txns.size() - 9]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 9]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_payment_channel_create()) &&
                     BEAST_EXPECT(res.payment_channel_create()
                                      .amount()
                                      .value()
                                      .has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.payment_channel_create()
                                .amount()
                                .value()
                                .xrp_amount()
                                .drops() == txnJson["Amount"].asUInt()) &&
                     BEAST_EXPECT(
                            res.payment_channel_create()
                                .destination()
                                .value()
                                .address() == txnJson["Destination"]) &&
                     BEAST_EXPECT(
                            res.payment_channel_create()
                                .settle_delay()
                                .value() == txnJson["SettleDelay"].asUInt()) &&
                     BEAST_EXPECT(
                            strHex(res.payment_channel_create()
                                       .public_key()
                                       .value()) == txnJson["PublicKey"]);
             }},
            {14,
             8,
             strHex(txns[txns.size() - 10]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 10]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_escrow_cancel()) &&
                     BEAST_EXPECT(
                            res.escrow_cancel().owner().value().address() ==
                            txnJson["Owner"]) &&
                     BEAST_EXPECT(
                            res.escrow_cancel().offer_sequence().value() ==
                            txnJson["OfferSequence"].asUInt()

                     );
             }},
            {13,
             8,
             strHex(txns[txns.size() - 11]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 11]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_escrow_finish()) &&
                     BEAST_EXPECT(
                            res.escrow_finish().owner().value().address() ==
                            txnJson["Owner"]) &&
                     BEAST_EXPECT(
                            res.escrow_finish().offer_sequence().value() ==
                            txnJson["OfferSequence"].asUInt()

                     );
             }},
            {12,
             7,
             strHex(txns[txns.size() - 12]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 12]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_escrow_create()) &&
                     BEAST_EXPECT(res.escrow_create()
                                      .amount()
                                      .value()
                                      .has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.escrow_create()
                                .amount()
                                .value()
                                .xrp_amount()
                                .drops() == txnJson["Amount"].asUInt()) &&
                     BEAST_EXPECT(
                            res.escrow_create()
                                .destination()
                                .value()
                                .address() == txnJson["Destination"]) &&
                     BEAST_EXPECT(
                            res.escrow_create().cancel_after().value() ==
                            txnJson["CancelAfter"].asUInt()) &&
                     BEAST_EXPECT(
                            res.escrow_create().finish_after().value() ==
                            txnJson["FinishAfter"].asUInt());
             }},
            {11,
             7,
             strHex(txns[txns.size() - 13]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 13]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_escrow_create()) &&
                     BEAST_EXPECT(res.escrow_create()
                                      .amount()
                                      .value()
                                      .has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.escrow_create()
                                .amount()
                                .value()
                                .xrp_amount()
                                .drops() == txnJson["Amount"].asUInt()) &&
                     BEAST_EXPECT(
                            res.escrow_create()
                                .destination()
                                .value()
                                .address() == txnJson["Destination"]) &&
                     BEAST_EXPECT(
                            res.escrow_create().finish_after().value() ==
                            txnJson["FinishAfter"].asUInt());
             }},
            {10,
             7,
             strHex(txns[txns.size() - 14]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 14]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_signer_list_set()) &&
                     BEAST_EXPECT(
                            res.signer_list_set().signer_quorum().value() ==
                            txnJson["SignerQuorum"].asUInt()) &&
                     BEAST_EXPECT(
                            res.signer_list_set().signer_entries().size() ==
                            3) &&
                     BEAST_EXPECT(
                            res.signer_list_set()
                                .signer_entries()[0]
                                .account()
                                .value()
                                .address() ==
                            txnJson["SignerEntries"][0u]["SignerEntry"]
                                   ["Account"]) &&
                     BEAST_EXPECT(
                            res.signer_list_set()
                                .signer_entries()[0]
                                .signer_weight()
                                .value() ==
                            txnJson["SignerEntries"][0u]["SignerEntry"]
                                   ["SignerWeight"]
                                       .asUInt()) &&
                     BEAST_EXPECT(
                            res.signer_list_set()
                                .signer_entries()[1]
                                .account()
                                .value()
                                .address() ==
                            txnJson["SignerEntries"][1u]["SignerEntry"]
                                   ["Account"]) &&
                     BEAST_EXPECT(
                            res.signer_list_set()
                                .signer_entries()[1]
                                .signer_weight()
                                .value() ==
                            txnJson["SignerEntries"][1u]["SignerEntry"]
                                   ["SignerWeight"]
                                       .asUInt()) &&
                     BEAST_EXPECT(
                            res.signer_list_set()
                                .signer_entries()[2]
                                .account()
                                .value()
                                .address() ==
                            txnJson["SignerEntries"][2u]["SignerEntry"]
                                   ["Account"]) &&
                     BEAST_EXPECT(
                            res.signer_list_set()
                                .signer_entries()[2]
                                .signer_weight()
                                .value() ==
                            txnJson["SignerEntries"][2u]["SignerEntry"]
                                   ["SignerWeight"]
                                       .asUInt());
             }},
            {9,
             6,
             strHex(txns[txns.size() - 15]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 15]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_offer_cancel()) &&
                     BEAST_EXPECT(
                            res.offer_cancel().offer_sequence().value() ==
                            txnJson["OfferSequence"].asUInt());
             }},
            {8,
             5,
             strHex(txns[txns.size() - 16]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 16]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_offer_create()) &&
                     BEAST_EXPECT(res.offer_create()
                                      .taker_gets()
                                      .value()
                                      .has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.offer_create()
                                .taker_gets()
                                .value()
                                .xrp_amount()
                                .drops() == txnJson["TakerGets"].asUInt()) &&
                     BEAST_EXPECT(res.offer_create()
                                      .taker_pays()
                                      .value()
                                      .has_issued_currency_amount()) &&
                     BEAST_EXPECT(
                            res.offer_create()
                                .taker_pays()
                                .value()
                                .issued_currency_amount()
                                .currency()
                                .name() == txnJson["TakerPays"]["currency"]) &&
                     BEAST_EXPECT(
                            res.offer_create()
                                .taker_pays()
                                .value()
                                .issued_currency_amount()
                                .value() == txnJson["TakerPays"]["value"]) &&
                     BEAST_EXPECT(
                            res.offer_create()
                                .taker_pays()
                                .value()
                                .issued_currency_amount()
                                .issuer()
                                .address() == txnJson["TakerPays"]["issuer"]);
             }},
            {7,
             5,
             strHex(txns[txns.size() - 17]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 17]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_trust_set()) &&
                     BEAST_EXPECT(res.trust_set()
                                      .limit_amount()
                                      .value()
                                      .has_issued_currency_amount()) &&
                     BEAST_EXPECT(
                            res.trust_set()
                                .limit_amount()
                                .value()
                                .issued_currency_amount()
                                .currency()
                                .name() ==
                            txnJson["LimitAmount"]["currency"]) &&
                     BEAST_EXPECT(
                            res.trust_set()
                                .limit_amount()
                                .value()
                                .issued_currency_amount()
                                .value() == txnJson["LimitAmount"]["value"]) &&
                     BEAST_EXPECT(
                            res.trust_set()
                                .limit_amount()
                                .value()
                                .issued_currency_amount()
                                .issuer()
                                .address() == txnJson["LimitAmount"]["issuer"]);
             }},
            {6,
             4,
             strHex(txns[txns.size() - 18]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 18]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_set_regular_key()) &&
                     BEAST_EXPECT(
                            res.set_regular_key()
                                .regular_key()
                                .value()
                                .address() == txnJson["RegularKey"]);
             }},
            {5,
             4,
             strHex(txns[txns.size() - 19]->getTransactionID()),
             [&txns, this](auto res) {
                 auto txnJson =
                     txns[txns.size() - 19]->getJson(JsonOptions::none);
                 return BEAST_EXPECT(res.has_payment()) &&
                     BEAST_EXPECT(
                            res.payment().amount().value().has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.payment()
                                .amount()
                                .value()
                                .xrp_amount()
                                .drops() == txnJson["Amount"].asUInt()) &&
                     BEAST_EXPECT(
                            res.payment().destination().value().address() ==
                            txnJson["Destination"]) &&
                     BEAST_EXPECT(res.has_source_tag()) &&
                     BEAST_EXPECT(
                            res.source_tag().value() ==
                            txnJson["SourceTag"].asUInt()) &&
                     BEAST_EXPECT(res.payment().has_destination_tag()) &&
                     BEAST_EXPECT(
                            res.payment().destination_tag().value() ==
                            txnJson["DestinationTag"].asUInt()) &&
                     BEAST_EXPECT(res.has_last_ledger_sequence()) &&
                     BEAST_EXPECT(
                            res.last_ledger_sequence().value() ==
                            txnJson["LastLedgerSequence"].asUInt()) &&
                     BEAST_EXPECT(res.has_transaction_signature()) &&
                     BEAST_EXPECT(res.has_account()) &&
                     BEAST_EXPECT(
                            res.account().value().address() ==
                            txnJson["Account"]) &&
                     BEAST_EXPECT(res.has_flags()) &&
                     BEAST_EXPECT(
                            res.flags().value() == txnJson["Flags"].asUInt());
             }},
            {4,
             4,
             strHex(txns[txns.size() - 20]->getTransactionID()),
             [this](auto res) { return BEAST_EXPECT(res.has_account_set()); }},
            {3,
             3,
             "9CE54C3B934E473A995B477E92EC229F99CED5B62BF4D2ACE4DC42719103AE2F",
             [this](auto res) {
                 return BEAST_EXPECT(res.has_account_set()) &&
                     BEAST_EXPECT(res.account_set().set_flag().value() == 8);
             }},
            {1,
             3,
             "2B5054734FA43C6C7B54F61944FAD6178ACD5D0272B39BA7FCD32A5D3932FBFF",
             [&alice, this](auto res) {
                 return BEAST_EXPECT(res.has_payment()) &&
                     BEAST_EXPECT(
                            res.payment().amount().value().has_xrp_amount()) &&
                     BEAST_EXPECT(
                            res.payment()
                                .amount()
                                .value()
                                .xrp_amount()
                                .drops() == 1000000000010) &&
                     BEAST_EXPECT(
                            res.payment().destination().value().address() ==
                            alice.human());
             }}};

        using MetaCheck =
            std::function<bool(org::xrpl::rpc::v1::Meta const& res)>;
        static const MetaCheck txMetaCheck[]{
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](org::xrpl::rpc::v1::AffectedNode const&
                                      entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 3) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DEPOSIT_PREAUTH;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),

                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 1) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 5) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_CHECK;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 2);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 5) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_CHECK;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 2);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 1) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 5) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_CHECK;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 2);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 5) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_CHECK;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 2);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 5) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_PAY_CHANNEL;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 2);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 2) &&

                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_PAY_CHANNEL;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 5) &&

                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_PAY_CHANNEL;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 2);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 1) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 3) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ESCROW;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 3) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ESCROW;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 2) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 3) &&

                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ESCROW;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 1) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 3) &&

                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ESCROW;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 3) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_SIGNER_LIST;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 4) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_OFFER;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 1) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 4) &&

                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_OFFER;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 5) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_DIRECTORY_NODE;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_RIPPLE_STATE;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 2) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 1) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 2);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 2) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 1) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 1);
            }},
            {[this](auto meta) {
                return BEAST_EXPECT(meta.transaction_index() == 0) &&
                    BEAST_EXPECT(meta.affected_nodes_size() == 2) &&
                    BEAST_EXPECT(
                           std::count_if(
                               meta.affected_nodes().begin(),
                               meta.affected_nodes().end(),
                               [](auto entry) {
                                   return entry.ledger_entry_type() ==
                                       org::xrpl::rpc::v1::LedgerEntryType::
                                           LEDGER_ENTRY_TYPE_ACCOUNT_ROOT;
                               }) == 2);
            }}};

        auto doCheck = [this](auto txn, auto txCheck) {
            return BEAST_EXPECT(txn.has_transaction()) &&
                BEAST_EXPECT(txn.validated()) &&
                BEAST_EXPECT(strHex(txn.hash()) == txCheck.hash) &&
                BEAST_EXPECT(txn.ledger_index() == txCheck.ledgerIndex) &&
                BEAST_EXPECT(
                       txn.transaction().sequence().value() ==
                       txCheck.sequence) &&
                txCheck.checkTxn(txn.transaction());
        };

        auto doMetaCheck = [this](auto txn, auto txMetaCheck) {
            return BEAST_EXPECT(txn.has_meta()) &&
                BEAST_EXPECT(txn.meta().has_transaction_result()) &&
                BEAST_EXPECT(
                       txn.meta().transaction_result().result_type() ==
                       org::xrpl::rpc::v1::TransactionResult::
                           RESULT_TYPE_TES) &&
                BEAST_EXPECT(
                       txn.meta().transaction_result().result() ==
                       "tesSUCCESS") &&
                txMetaCheck(txn.meta());
        };

        auto [res, status] = next(grpcPort, env, alice.human());

        if (!BEAST_EXPECT(status.error_code() == 0))
            return;

        if (!BEAST_EXPECT(
                res.transactions().size() ==
                std::extent<decltype(txCheck)>::value))
            return;
        for (int i = 0; i < res.transactions().size(); ++i)
        {
            BEAST_EXPECT(doCheck(res.transactions()[i], txCheck[i]));
            BEAST_EXPECT(doMetaCheck(res.transactions()[i], txMetaCheck[i]));
        }

        // test binary representation
        std::tie(res, status) = nextBinary(grpcPort, env, alice.human());

        // txns vector does not contain the first two transactions returned by
        // account_tx
        if (!BEAST_EXPECT(res.transactions().size() == txns.size() + 2))
            return;

        std::reverse(txns.begin(), txns.end());
        for (int i = 0; i < txns.size(); ++i)
        {
            auto toByteString = [](auto data) {
                const char* bytes = reinterpret_cast<const char*>(data.data());
                return std::string(bytes, data.size());
            };

            auto tx = txns[i];
            Serializer s = tx->getSerializer();
            std::string bin = toByteString(s);

            BEAST_EXPECT(res.transactions(i).transaction_binary() == bin);
        }
    }

    void
    testAccountTxPagingGrpc()
    {
        testcase("Test Account_tx Grpc");

        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));

        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};

        env.fund(XRP(10000), A1, A2, A3);
        env.close();

        env.trust(A3["USD"](1000), A1);
        env.trust(A2["USD"](1000), A1);
        env.trust(A3["USD"](1000), A2);
        env.close();

        for (auto i = 0; i < 5; ++i)
        {
            env(pay(A2, A1, A2["USD"](2)));
            env(pay(A3, A1, A3["USD"](2)));
            env(offer(A1, XRP(11), A1["USD"](1)));
            env(offer(A2, XRP(10), A2["USD"](1)));
            env(offer(A3, XRP(9), A3["USD"](1)));
            env.close();
        }

        /* The sequence/ledger for A3 are as follows:
         * seq     ledger_index
         * 3  ----> 3
         * 1  ----> 3
         * 2  ----> 4
         * 2  ----> 4
         * 2  ----> 5
         * 3  ----> 5
         * 4  ----> 6
         * 5  ----> 6
         * 6  ----> 7
         * 7  ----> 7
         * 8  ----> 8
         * 9  ----> 8
         * 10 ----> 9
         * 11 ----> 9
         */

        // page through the results in several ways.
        {
            // limit = 2, 3 batches giving the first 6 txs
            auto [res, status] = next(grpcPort, env, A3.human(), 2, 5, 2, true);

            auto txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 2))
                return;

            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            BEAST_EXPECT(checkTransaction(txs[1u], 3, 3));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort, env, A3.human(), 2, 5, 2, true, res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[1u], 4, 4));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort, env, A3.human(), 2, 5, 2, true, res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 5));
            BEAST_EXPECT(checkTransaction(txs[1u], 5, 5));
            BEAST_EXPECT(!res.has_marker());
            return;
        }

        {
            // limit 1, 3 requests giving the first 3 txs
            auto [res, status] = next(grpcPort, env, A3.human(), 3, 9, 1, true);
            auto txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort, env, A3.human(), 3, 9, 1, true, res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort, env, A3.human(), 3, 9, 1, true, res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            // continue with limit 3, to end of all txs
            std::tie(res, status) = next(
                grpcPort, env, A3.human(), 3, 9, 3, true, res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[1u], 4, 5));
            BEAST_EXPECT(checkTransaction(txs[2u], 5, 5));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort, env, A3.human(), 3, 9, 3, true, res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 6, 6));
            BEAST_EXPECT(checkTransaction(txs[1u], 7, 6));
            BEAST_EXPECT(checkTransaction(txs[2u], 8, 7));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort, env, A3.human(), 3, 9, 3, true, res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 9, 7));
            BEAST_EXPECT(checkTransaction(txs[1u], 10, 8));
            BEAST_EXPECT(checkTransaction(txs[2u], 11, 8));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort, env, A3.human(), 3, 9, 3, true, res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 12, 9));
            BEAST_EXPECT(checkTransaction(txs[1u], 13, 9));
            BEAST_EXPECT(!res.has_marker());
        }

        {
            // limit 2, descending, 2 batches giving last 4 txs
            auto [res, status] =
                next(grpcPort, env, A3.human(), 3, 9, 2, false);
            auto txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 13, 9));
            BEAST_EXPECT(checkTransaction(txs[1u], 12, 9));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort,
                env,
                A3.human(),
                3,
                9,
                2,
                false,
                res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 11, 8));
            BEAST_EXPECT(checkTransaction(txs[1u], 10, 8));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            // continue with limit 3 until all txs have been seen
            std::tie(res, status) = next(
                grpcPort,
                env,
                A3.human(),
                3,
                9,
                3,
                false,
                res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 9, 7));
            BEAST_EXPECT(checkTransaction(txs[1u], 8, 7));
            BEAST_EXPECT(checkTransaction(txs[2u], 7, 6));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort,
                env,
                A3.human(),
                3,
                9,
                3,
                false,
                res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 6, 6));
            BEAST_EXPECT(checkTransaction(txs[1u], 5, 5));
            BEAST_EXPECT(checkTransaction(txs[2u], 4, 5));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort,
                env,
                A3.human(),
                3,
                9,
                3,
                false,
                res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[1u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[2u], 3, 3));
            if (!BEAST_EXPECT(res.has_marker()))
                return;

            std::tie(res, status) = next(
                grpcPort,
                env,
                A3.human(),
                3,
                9,
                3,
                false,
                res.mutable_marker());
            txs = res.transactions();
            if (!BEAST_EXPECT(txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            BEAST_EXPECT(!res.has_marker());
        }
    }

public:
    void
    run() override
    {
        testAccountTxPaging();
        testAccountTxPagingGrpc();
        testAccountTxParametersGrpc();
        testAccountTxContentsGrpc();
    }
};

BEAST_DEFINE_TESTSUITE(AccountTxPaging, app, ripple);

}  // namespace ripple
