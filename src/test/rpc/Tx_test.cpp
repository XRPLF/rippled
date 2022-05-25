//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/app/rdb/backend/SQLiteDatabase.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/GRPCHelpers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/rpc/GRPCTestClientBase.h>

#include <string>

namespace ripple {
namespace test {

class Tx_test : public beast::unit_test::suite
{
    template <class T>
    std::string
    toByteString(T const& data)
    {
        const char* bytes = reinterpret_cast<const char*>(data.data());
        return {bytes, data.size()};
    }

    void
    cmpAmount(
        const org::xrpl::rpc::v1::CurrencyAmount& proto_amount,
        STAmount amount)
    {
        if (amount.native())
        {
            if (!BEAST_EXPECT(proto_amount.has_xrp_amount()))
                return;
            BEAST_EXPECT(
                proto_amount.xrp_amount().drops() == amount.xrp().drops());
        }
        else
        {
            if (!BEAST_EXPECT(proto_amount.has_issued_currency_amount()))
                return;

            org::xrpl::rpc::v1::IssuedCurrencyAmount issuedCurrency =
                proto_amount.issued_currency_amount();
            Issue const& issue = amount.issue();
            Currency currency = issue.currency;
            BEAST_EXPECT(
                issuedCurrency.currency().name() == to_string(currency));
            BEAST_EXPECT(
                issuedCurrency.currency().code() == toByteString(currency));
            BEAST_EXPECT(issuedCurrency.value() == to_string(amount.iou()));
            BEAST_EXPECT(
                issuedCurrency.issuer().address() == toBase58(issue.account));
        }
    }

    void
    cmpPaymentTx(
        const org::xrpl::rpc::v1::Transaction& proto,
        std::shared_ptr<STTx const> txnSt)
    {
        if (!BEAST_EXPECT(proto.has_payment()))
            return;

        if (!BEAST_EXPECT(
                safe_cast<TxType>(txnSt->getFieldU16(sfTransactionType)) ==
                TxType::ttPAYMENT))
            return;

        AccountID account = txnSt->getAccountID(sfAccount);

        if (!BEAST_EXPECT(proto.has_account()))
            return;
        BEAST_EXPECT(proto.account().value().address() == toBase58(account));

        STAmount amount = txnSt->getFieldAmount(sfAmount);
        if (!BEAST_EXPECT(proto.payment().has_amount()))
            return;
        cmpAmount(proto.payment().amount().value(), amount);

        AccountID accountDest = txnSt->getAccountID(sfDestination);
        if (!BEAST_EXPECT(proto.payment().has_destination()))
            return;
        BEAST_EXPECT(
            proto.payment().destination().value().address() ==
            toBase58(accountDest));

        STAmount fee = txnSt->getFieldAmount(sfFee);
        if (!BEAST_EXPECT(proto.has_fee()))
            return;
        BEAST_EXPECT(proto.fee().drops() == fee.xrp().drops());

        if (!BEAST_EXPECT(proto.has_sequence()))
            return;
        BEAST_EXPECT(
            proto.sequence().value() == txnSt->getFieldU32(sfSequence));

        if (!BEAST_EXPECT(proto.has_signing_public_key()))
            return;

        Blob signingPubKey = txnSt->getFieldVL(sfSigningPubKey);
        BEAST_EXPECT(
            proto.signing_public_key().value() == toByteString(signingPubKey));

        if (txnSt->isFieldPresent(sfFlags))
        {
            if (!BEAST_EXPECT(proto.has_flags()))
                return;
            BEAST_EXPECT(proto.flags().value() == txnSt->getFieldU32(sfFlags));
        }
        else
        {
            BEAST_EXPECT(!proto.has_flags());
        }

        if (txnSt->isFieldPresent(sfLastLedgerSequence))
        {
            if (!BEAST_EXPECT(proto.has_last_ledger_sequence()))
                return;

            BEAST_EXPECT(
                proto.last_ledger_sequence().value() ==
                txnSt->getFieldU32(sfLastLedgerSequence));
        }
        else
        {
            BEAST_EXPECT(!proto.has_last_ledger_sequence());
        }

        if (txnSt->isFieldPresent(sfTxnSignature))
        {
            if (!BEAST_EXPECT(proto.has_transaction_signature()))
                return;

            Blob blob = txnSt->getFieldVL(sfTxnSignature);
            BEAST_EXPECT(
                proto.transaction_signature().value() == toByteString(blob));
        }

        if (txnSt->isFieldPresent(sfSendMax))
        {
            if (!BEAST_EXPECT(proto.payment().has_send_max()))
                return;
            STAmount const& send_max = txnSt->getFieldAmount(sfSendMax);
            cmpAmount(proto.payment().send_max().value(), send_max);
        }
        else
        {
            BEAST_EXPECT(!proto.payment().has_send_max());
        }

        if (txnSt->isFieldPresent(sfAccountTxnID))
        {
            if (!BEAST_EXPECT(proto.has_account_transaction_id()))
                return;
            auto field = txnSt->getFieldH256(sfAccountTxnID);
            BEAST_EXPECT(
                proto.account_transaction_id().value() == toByteString(field));
        }
        else
        {
            BEAST_EXPECT(!proto.has_account_transaction_id());
        }

        if (txnSt->isFieldPresent(sfSourceTag))
        {
            if (!BEAST_EXPECT(proto.has_source_tag()))
                return;
            BEAST_EXPECT(
                proto.source_tag().value() == txnSt->getFieldU32(sfSourceTag));
        }
        else
        {
            BEAST_EXPECT(!proto.has_source_tag());
        }

        if (txnSt->isFieldPresent(sfDestinationTag))
        {
            if (!BEAST_EXPECT(proto.payment().has_destination_tag()))
                return;

            BEAST_EXPECT(
                proto.payment().destination_tag().value() ==
                txnSt->getFieldU32(sfDestinationTag));
        }
        else
        {
            BEAST_EXPECT(!proto.payment().has_destination_tag());
        }

        if (txnSt->isFieldPresent(sfInvoiceID))
        {
            if (!BEAST_EXPECT(proto.payment().has_invoice_id()))
                return;

            auto field = txnSt->getFieldH256(sfInvoiceID);
            BEAST_EXPECT(
                proto.payment().invoice_id().value() == toByteString(field));
        }
        else
        {
            BEAST_EXPECT(!proto.payment().has_invoice_id());
        }

        if (txnSt->isFieldPresent(sfDeliverMin))
        {
            if (!BEAST_EXPECT(proto.payment().has_deliver_min()))
                return;
            STAmount const& deliverMin = txnSt->getFieldAmount(sfDeliverMin);
            cmpAmount(proto.payment().deliver_min().value(), deliverMin);
        }
        else
        {
            BEAST_EXPECT(!proto.payment().has_deliver_min());
        }

        STPathSet const& pathset = txnSt->getFieldPathSet(sfPaths);
        if (!BEAST_EXPECT(pathset.size() == proto.payment().paths_size()))
            return;

        int ind = 0;
        for (auto it = pathset.begin(); it < pathset.end(); ++it)
        {
            STPath const& path = *it;

            const org::xrpl::rpc::v1::Payment_Path& protoPath =
                proto.payment().paths(ind++);
            if (!BEAST_EXPECT(protoPath.elements_size() == path.size()))
                continue;

            int ind2 = 0;
            for (auto it2 = path.begin(); it2 != path.end(); ++it2)
            {
                const org::xrpl::rpc::v1::Payment_PathElement& protoElement =
                    protoPath.elements(ind2++);
                STPathElement const& elt = *it2;

                if (elt.isOffer())
                {
                    if (elt.hasCurrency())
                    {
                        Currency const& currency = elt.getCurrency();
                        if (BEAST_EXPECT(protoElement.has_currency()))
                        {
                            BEAST_EXPECT(
                                protoElement.currency().name() ==
                                to_string(currency));
                        }
                    }
                    else
                    {
                        BEAST_EXPECT(!protoElement.has_currency());
                    }
                    if (elt.hasIssuer())
                    {
                        AccountID const& issuer = elt.getIssuerID();
                        if (BEAST_EXPECT(protoElement.has_issuer()))
                        {
                            BEAST_EXPECT(
                                protoElement.issuer().address() ==
                                toBase58(issuer));
                        }
                    }
                    else
                    {
                        BEAST_EXPECT(!protoElement.has_issuer());
                    }
                }
                else
                {
                    if (BEAST_EXPECT(protoElement.has_account()))
                    {
                        AccountID const& path_account = elt.getAccountID();
                        BEAST_EXPECT(
                            protoElement.account().address() ==
                            toBase58(path_account));
                    }
                    else
                    {
                        BEAST_EXPECT(!protoElement.has_account());
                    }

                    BEAST_EXPECT(!protoElement.has_issuer());
                    BEAST_EXPECT(!protoElement.has_currency());
                }
            }
        }

        if (txnSt->isFieldPresent(sfMemos))
        {
            auto arr = txnSt->getFieldArray(sfMemos);
            if (BEAST_EXPECT(proto.memos_size() == arr.size()))
            {
                for (size_t i = 0; i < arr.size(); ++i)
                {
                    auto protoMemo = proto.memos(i);
                    auto stMemo = arr[i];

                    if (stMemo.isFieldPresent(sfMemoData))
                    {
                        if (BEAST_EXPECT(protoMemo.has_memo_data()))
                        {
                            BEAST_EXPECT(
                                protoMemo.memo_data().value() ==
                                toByteString(stMemo.getFieldVL(sfMemoData)));
                        }
                    }
                    else
                    {
                        BEAST_EXPECT(!protoMemo.has_memo_data());
                    }

                    if (stMemo.isFieldPresent(sfMemoType))
                    {
                        if (BEAST_EXPECT(protoMemo.has_memo_type()))
                        {
                            BEAST_EXPECT(
                                protoMemo.memo_type().value() ==
                                toByteString(stMemo.getFieldVL(sfMemoType)));
                        }
                    }
                    else
                    {
                        BEAST_EXPECT(!protoMemo.has_memo_type());
                    }

                    if (stMemo.isFieldPresent(sfMemoFormat))
                    {
                        if (BEAST_EXPECT(protoMemo.has_memo_format()))
                        {
                            BEAST_EXPECT(
                                protoMemo.memo_format().value() ==
                                toByteString(stMemo.getFieldVL(sfMemoFormat)));
                        }
                    }
                    else
                    {
                        BEAST_EXPECT(!protoMemo.has_memo_format());
                    }
                }
            }
        }
        else
        {
            BEAST_EXPECT(proto.memos_size() == 0);
        }

        if (txnSt->isFieldPresent(sfSigners))
        {
            auto arr = txnSt->getFieldArray(sfSigners);
            if (BEAST_EXPECT(proto.signers_size() == arr.size()))
            {
                for (size_t i = 0; i < arr.size(); ++i)
                {
                    auto protoSigner = proto.signers(i);
                    auto stSigner = arr[i];

                    if (stSigner.isFieldPresent(sfAccount))
                    {
                        if (BEAST_EXPECT(protoSigner.has_account()))
                        {
                            BEAST_EXPECT(
                                protoSigner.account().value().address() ==
                                toBase58(stSigner.getAccountID(sfAccount)));
                        }
                    }
                    else
                    {
                        BEAST_EXPECT(!protoSigner.has_account());
                    }

                    if (stSigner.isFieldPresent(sfTxnSignature))
                    {
                        if (BEAST_EXPECT(
                                protoSigner.has_transaction_signature()))
                        {
                            Blob blob = stSigner.getFieldVL(sfTxnSignature);
                            BEAST_EXPECT(
                                protoSigner.transaction_signature().value() ==
                                toByteString(blob));
                        }
                    }
                    else
                    {
                        BEAST_EXPECT(!protoSigner.has_transaction_signature());
                    }

                    if (stSigner.isFieldPresent(sfSigningPubKey))
                    {
                        if (BEAST_EXPECT(protoSigner.has_signing_public_key()))
                        {
                            Blob signingPubKey =
                                stSigner.getFieldVL(sfSigningPubKey);
                            BEAST_EXPECT(
                                protoSigner.signing_public_key().value() ==
                                toByteString(signingPubKey));
                        }
                    }
                    else
                    {
                        BEAST_EXPECT(!protoSigner.has_signing_public_key());
                    }
                }
            }
        }
        else
        {
            BEAST_EXPECT(proto.signers_size() == 0);
        }
    }

    void
    cmpMeta(
        const org::xrpl::rpc::v1::Meta& proto,
        std::shared_ptr<TxMeta> txMeta)
    {
        BEAST_EXPECT(proto.transaction_index() == txMeta->getIndex());
        BEAST_EXPECT(
            proto.transaction_result().result() ==
            transToken(txMeta->getResultTER()));

        org::xrpl::rpc::v1::TransactionResult r;

        RPC::convert(r, txMeta->getResultTER());

        BEAST_EXPECT(
            proto.transaction_result().result_type() == r.result_type());
    }

    void
    cmpDeliveredAmount(
        const org::xrpl::rpc::v1::Meta& meta,
        const org::xrpl::rpc::v1::Transaction& txn,
        const std::shared_ptr<TxMeta> expMeta,
        const std::shared_ptr<STTx const> expTxn,
        bool checkAmount = true)
    {
        if (expMeta->hasDeliveredAmount())
        {
            if (!BEAST_EXPECT(meta.has_delivered_amount()))
                return;
            cmpAmount(
                meta.delivered_amount().value(), expMeta->getDeliveredAmount());
        }
        else
        {
            if (expTxn->isFieldPresent(sfAmount))
            {
                using namespace std::chrono_literals;
                if (checkAmount)
                {
                    cmpAmount(
                        meta.delivered_amount().value(),
                        expTxn->getFieldAmount(sfAmount));
                }
            }
            else
            {
                BEAST_EXPECT(!meta.has_delivered_amount());
            }
        }
    }

    // gRPC stuff
    class GrpcTxClient : public GRPCTestClientBase
    {
    public:
        org::xrpl::rpc::v1::GetTransactionRequest request;
        org::xrpl::rpc::v1::GetTransactionResponse reply;

        explicit GrpcTxClient(std::string const& port)
            : GRPCTestClientBase(port)
        {
        }

        void
        Tx()
        {
            status = stub_->GetTransaction(&context, request, &reply);
        }
    };

    class GrpcAccountTxClient : public GRPCTestClientBase
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

    void
    testTxGrpc()
    {
        testcase("Test Tx Grpc");

        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));

        using namespace std::chrono_literals;
        // Set time to this value (or greater) to get delivered_amount in meta
        env.timeKeeper().set(NetClock::time_point{446000001s});

        auto grpcTx = [&grpcPort](auto hash, auto binary) {
            GrpcTxClient client(grpcPort);
            client.request.set_hash(&hash, sizeof(hash));
            client.request.set_binary(binary);
            client.Tx();
            return std::pair<bool, org::xrpl::rpc::v1::GetTransactionResponse>(
                client.status.ok(), client.reply);
        };

        Account A1{"A1"};
        Account A2{"A2"};
        Account A3{"A3"};
        env.fund(XRP(10000), A1);
        env.fund(XRP(10000), A2);
        env.close();
        env.trust(A2["USD"](1000), A1);
        env.close();
        env(fset(A2, 5));  // set asfAccountTxnID flag

        // SignerListSet
        env(signers(A2, 1, {{"bogie", 1}, {"demon", 1}, {A1, 1}, {A3, 1}}),
            sig(A2));
        env.close();
        std::vector<std::shared_ptr<STTx const>> txns;
        auto const startLegSeq = env.current()->info().seq;

        uint256 prevHash;
        for (int i = 0; i < 14; ++i)
        {
            auto const baseFee = env.current()->fees().base;
            auto txfee = fee(i + (2 * baseFee));
            auto lls = last_ledger_seq(i + startLegSeq + 20);
            auto dsttag = dtag(i * 456);
            auto srctag = stag(i * 321);
            auto sm = sendmax(A2["USD"](1000));
            auto dm = delivermin(A2["USD"](50));
            auto txf = txflags(131072);  // partial payment flag
            auto txnid = account_txn_id(prevHash);
            auto inv = invoice_id(prevHash);
            auto mem1 = memo("foo", "bar", "baz");
            auto mem2 = memo("dragons", "elves", "goblins");

            if (i & 1)
            {
                if (i & 2)
                {
                    env(pay(A2, A1, A2["USD"](100)),
                        txfee,
                        srctag,
                        dsttag,
                        lls,
                        sm,
                        dm,
                        txf,
                        txnid,
                        inv,
                        mem1,
                        mem2,
                        sig(A2));
                }
                else
                {
                    env(pay(A2, A1, A2["USD"](100)),
                        txfee,
                        srctag,
                        dsttag,
                        lls,
                        sm,
                        dm,
                        txf,
                        txnid,
                        inv,
                        mem1,
                        mem2,
                        msig(A3));
                }
            }
            else
            {
                if (i & 2)
                {
                    env(pay(A2, A1, A2["XRP"](200)),
                        txfee,
                        srctag,
                        dsttag,
                        lls,
                        txnid,
                        inv,
                        mem1,
                        mem2,
                        sig(A2));
                }
                else
                {
                    env(pay(A2, A1, A2["XRP"](200)),
                        txfee,
                        srctag,
                        dsttag,
                        lls,
                        txnid,
                        inv,
                        mem1,
                        mem2,
                        msig(A3));
                }
            }
            txns.emplace_back(env.tx());
            prevHash = txns.back()->getTransactionID();
            env.close();
        }

        // Payment with Paths
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(10000), "alice", "bob", gw);
        env.trust(USD(600), "alice");
        env.trust(USD(700), "bob");
        env(pay(gw, "alice", USD(70)));
        txns.emplace_back(env.tx());
        env.close();
        env(pay(gw, "bob", USD(50)));
        txns.emplace_back(env.tx());
        env.close();
        env(pay("alice", "bob", Account("bob")["USD"](5)), path(gw));
        txns.emplace_back(env.tx());
        env.close();

        auto const endLegSeq = env.closed()->info().seq;

        // Find the existing transactions
        auto& ledgerMaster = env.app().getLedgerMaster();
        int index = startLegSeq;
        for (auto&& tx : txns)
        {
            auto id = tx->getTransactionID();
            auto ledger = ledgerMaster.getLedgerBySeq(index);

            for (bool b : {false, true})
            {
                auto const result = grpcTx(id, b);

                BEAST_EXPECT(result.first == true);
                BEAST_EXPECT(result.second.ledger_index() == index);
                BEAST_EXPECT(result.second.validated() == true);
                if (b)
                {
                    Serializer s = tx->getSerializer();
                    BEAST_EXPECT(
                        result.second.transaction_binary() == toByteString(s));
                }
                else
                {
                    cmpPaymentTx(result.second.transaction(), tx);
                }

                if (!ledger || b)
                    continue;

                auto rawMeta = ledger->txRead(id).second;
                if (!rawMeta)
                    continue;

                auto txMeta =
                    std::make_shared<TxMeta>(id, ledger->seq(), *rawMeta);

                cmpMeta(result.second.meta(), txMeta);
                cmpDeliveredAmount(
                    result.second.meta(),
                    result.second.transaction(),
                    txMeta,
                    tx);

                auto grpcAccountTx = [&grpcPort](
                                         uint256 const& id,
                                         bool binary,
                                         AccountID const& account)
                    -> std::
                        pair<bool, org::xrpl::rpc::v1::GetTransactionResponse> {
                            GrpcAccountTxClient client(grpcPort);
                            client.request.set_binary(binary);
                            client.request.mutable_account()->set_address(
                                toBase58(account));
                            client.AccountTx();
                            org::xrpl::rpc::v1::GetTransactionResponse res;

                            for (auto const& tx : client.reply.transactions())
                            {
                                if (uint256::fromVoid(tx.hash().data()) == id)
                                {
                                    return {client.status.ok(), tx};
                                }
                            }
                            return {false, res};
                        };

                // Compare result to result from account_tx
                auto mentioned = tx->getMentionedAccounts();

                if (!BEAST_EXPECT(mentioned.size()))
                    continue;

                auto account = *mentioned.begin();
                auto const accountTxResult = grpcAccountTx(id, b, account);

                if (!BEAST_EXPECT(accountTxResult.first))
                    continue;

                cmpPaymentTx(accountTxResult.second.transaction(), tx);
                cmpMeta(accountTxResult.second.meta(), txMeta);
                cmpDeliveredAmount(
                    accountTxResult.second.meta(),
                    accountTxResult.second.transaction(),
                    txMeta,
                    tx);
            }
            index++;
        }

        // Find not existing transaction
        auto const tx = env.jt(noop(A1), seq(env.seq(A1))).stx;
        for (bool b : {false, true})
        {
            auto const result = grpcTx(tx->getTransactionID(), b);

            BEAST_EXPECT(result.first == false);
        }

        // Delete one transaction
        const auto deletedLedger = (startLegSeq + endLegSeq) / 2;
        {
            // Remove one of the ledgers from the database directly
            dynamic_cast<SQLiteDatabase*>(&env.app().getRelationalDatabase())
                ->deleteTransactionByLedgerSeq(deletedLedger);
        }

        for (bool b : {false, true})
        {
            auto const result = grpcTx(tx->getTransactionID(), b);

            BEAST_EXPECT(result.first == false);
        }

        // non final transaction
        env(pay(A2, A1, A2["XRP"](200)));
        auto res = grpcTx(env.tx()->getTransactionID(), false);
        BEAST_EXPECT(res.first);
        BEAST_EXPECT(res.second.has_transaction());
        if (!BEAST_EXPECT(res.second.has_meta()))
            return;
        if (!BEAST_EXPECT(res.second.meta().has_transaction_result()))
            return;

        BEAST_EXPECT(
            res.second.meta().transaction_result().result() == "tesSUCCESS");
        BEAST_EXPECT(
            res.second.meta().transaction_result().result_type() ==
            org::xrpl::rpc::v1::TransactionResult::RESULT_TYPE_TES);
        BEAST_EXPECT(!res.second.validated());
        BEAST_EXPECT(!res.second.meta().has_delivered_amount());
        env.close();

        res = grpcTx(env.tx()->getTransactionID(), false);
        BEAST_EXPECT(res.first);
        BEAST_EXPECT(res.second.has_transaction());
        if (!BEAST_EXPECT(res.second.has_meta()))
            return;
        if (!BEAST_EXPECT(res.second.meta().has_transaction_result()))
            return;

        BEAST_EXPECT(
            res.second.meta().transaction_result().result() == "tesSUCCESS");
        BEAST_EXPECT(
            res.second.meta().transaction_result().result_type() ==
            org::xrpl::rpc::v1::TransactionResult::RESULT_TYPE_TES);
        BEAST_EXPECT(res.second.validated());
        BEAST_EXPECT(res.second.meta().has_delivered_amount());
    }

public:
    void
    run() override
    {
        testTxGrpc();
    }
};

BEAST_DEFINE_TESTSUITE(Tx, app, ripple);
}  // namespace test
}  // namespace ripple
