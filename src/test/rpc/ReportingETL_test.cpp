
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

#include <ripple/app/reporting/P2pProxy.h>
#include <ripple/beast/unit_test.h>
#include <ripple/rpc/impl/Tuning.h>

#include <ripple/core/ConfigSections.h>
#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/rpc/GRPCTestClientBase.h>

namespace ripple {
namespace test {

class ReportingETL_test : public beast::unit_test::suite
{
    // gRPC stuff
    class GrpcLedgerClient : public GRPCTestClientBase
    {
    public:
        org::xrpl::rpc::v1::GetLedgerRequest request;
        org::xrpl::rpc::v1::GetLedgerResponse reply;

        explicit GrpcLedgerClient(std::string const& port)
            : GRPCTestClientBase(port)
        {
        }

        void
        GetLedger()
        {
            status = stub_->GetLedger(&context, request, &reply);
        }
    };
    void
    testGetLedger()
    {
        testcase("GetLedger");
        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort =
            *(*config)[SECTION_PORT_GRPC].get<std::string>("port");
        Env env(*this, std::move(config));

        env.close();

        auto ledger = env.app().getLedgerMaster().getLedgerBySeq(3);

        BEAST_EXPECT(env.current()->info().seq == 4);

        auto grpcLedger = [&grpcPort](
                              auto sequence,
                              bool transactions,
                              bool expand,
                              bool get_objects,
                              bool get_object_neighbors) {
            GrpcLedgerClient grpcClient{grpcPort};

            grpcClient.request.mutable_ledger()->set_sequence(sequence);
            grpcClient.request.set_transactions(transactions);
            grpcClient.request.set_expand(expand);
            grpcClient.request.set_get_objects(get_objects);
            grpcClient.request.set_get_object_neighbors(get_object_neighbors);

            grpcClient.GetLedger();
            return std::make_pair(grpcClient.status, grpcClient.reply);
        };

        {
            auto [status, reply] = grpcLedger(3, false, false, false, false);

            BEAST_EXPECT(status.ok());
            BEAST_EXPECT(reply.validated());
            BEAST_EXPECT(!reply.has_hashes_list());
            BEAST_EXPECT(!reply.has_transactions_list());
            BEAST_EXPECT(!reply.skiplist_included());
            BEAST_EXPECT(reply.ledger_objects().objects_size() == 0);

            Serializer s;
            addRaw(ledger->info(), s, true);
            BEAST_EXPECT(s.slice() == makeSlice(reply.ledger_header()));
        }

        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice);
        env.fund(XRP(10000), bob);
        env.close();

        ledger = env.app().getLedgerMaster().getLedgerBySeq(4);

        std::vector<uint256> hashes;
        std::vector<std::shared_ptr<const STTx>> transactions;
        std::vector<std::shared_ptr<const STObject>> metas;
        for (auto& [sttx, meta] : ledger->txs)
        {
            hashes.push_back(sttx->getTransactionID());
            transactions.push_back(sttx);
            metas.push_back(meta);
        }

        Serializer s;
        addRaw(ledger->info(), s, true);

        {
            auto [status, reply] = grpcLedger(4, true, false, false, false);
            BEAST_EXPECT(status.ok());
            BEAST_EXPECT(reply.validated());
            BEAST_EXPECT(reply.has_hashes_list());
            BEAST_EXPECT(reply.hashes_list().hashes_size() == hashes.size());
            BEAST_EXPECT(
                uint256::fromVoid(reply.hashes_list().hashes(0).data()) ==
                hashes[0]);
            BEAST_EXPECT(
                uint256::fromVoid(reply.hashes_list().hashes(1).data()) ==
                hashes[1]);
            BEAST_EXPECT(
                uint256::fromVoid(reply.hashes_list().hashes(2).data()) ==
                hashes[2]);
            BEAST_EXPECT(
                uint256::fromVoid(reply.hashes_list().hashes(3).data()) ==
                hashes[3]);

            BEAST_EXPECT(!reply.has_transactions_list());
            BEAST_EXPECT(!reply.skiplist_included());
            BEAST_EXPECT(reply.ledger_objects().objects_size() == 0);

            BEAST_EXPECT(s.slice() == makeSlice(reply.ledger_header()));
        }

        {
            auto [status, reply] = grpcLedger(4, true, true, false, false);

            BEAST_EXPECT(status.ok());
            BEAST_EXPECT(reply.validated());
            BEAST_EXPECT(!reply.has_hashes_list());

            BEAST_EXPECT(reply.has_transactions_list());
            BEAST_EXPECT(reply.transactions_list().transactions_size() == 4);

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(0)
                              .transaction_blob()) ==
                transactions[0]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(0)
                              .metadata_blob()) ==
                metas[0]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(1)
                              .transaction_blob()) ==
                transactions[1]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(1)
                              .metadata_blob()) ==
                metas[1]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(2)
                              .transaction_blob()) ==
                transactions[2]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(2)
                              .metadata_blob()) ==
                metas[2]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(3)
                              .transaction_blob()) ==
                transactions[3]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(3)
                              .metadata_blob()) ==
                metas[3]->getSerializer().slice());

            BEAST_EXPECT(!reply.skiplist_included());
            BEAST_EXPECT(reply.ledger_objects().objects_size() == 0);

            BEAST_EXPECT(s.slice() == makeSlice(reply.ledger_header()));
        }

        {
            auto [status, reply] = grpcLedger(4, true, true, true, false);

            BEAST_EXPECT(status.ok());
            BEAST_EXPECT(reply.validated());
            BEAST_EXPECT(!reply.has_hashes_list());

            BEAST_EXPECT(reply.has_transactions_list());
            BEAST_EXPECT(reply.transactions_list().transactions_size() == 4);

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(0)
                              .transaction_blob()) ==
                transactions[0]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(0)
                              .metadata_blob()) ==
                metas[0]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(1)
                              .transaction_blob()) ==
                transactions[1]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(1)
                              .metadata_blob()) ==
                metas[1]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(2)
                              .transaction_blob()) ==
                transactions[2]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(2)
                              .metadata_blob()) ==
                metas[2]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(3)
                              .transaction_blob()) ==
                transactions[3]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(3)
                              .metadata_blob()) ==
                metas[3]->getSerializer().slice());
            BEAST_EXPECT(reply.skiplist_included());

            BEAST_EXPECT(s.slice() == makeSlice(reply.ledger_header()));

            auto parent = env.app().getLedgerMaster().getLedgerBySeq(3);

            SHAMap::Delta differences;

            int maxDifferences = std::numeric_limits<int>::max();

            bool res = parent->stateMap().compare(
                ledger->stateMap(), differences, maxDifferences);
            BEAST_EXPECT(res);

            size_t idx = 0;
            for (auto& [k, v] : differences)
            {
                BEAST_EXPECT(
                    k ==
                    uint256::fromVoid(
                        reply.ledger_objects().objects(idx).key().data()));
                if (v.second)
                {
                    BEAST_EXPECT(
                        v.second->slice() ==
                        makeSlice(reply.ledger_objects().objects(idx).data()));
                }
                ++idx;
            }
        }
        {
            auto [status, reply] = grpcLedger(4, true, true, true, true);

            BEAST_EXPECT(status.ok());
            BEAST_EXPECT(reply.validated());
            BEAST_EXPECT(!reply.has_hashes_list());
            BEAST_EXPECT(reply.object_neighbors_included());

            BEAST_EXPECT(reply.has_transactions_list());
            BEAST_EXPECT(reply.transactions_list().transactions_size() == 4);

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(0)
                              .transaction_blob()) ==
                transactions[0]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(0)
                              .metadata_blob()) ==
                metas[0]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(1)
                              .transaction_blob()) ==
                transactions[1]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(1)
                              .metadata_blob()) ==
                metas[1]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(2)
                              .transaction_blob()) ==
                transactions[2]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(2)
                              .metadata_blob()) ==
                metas[2]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(3)
                              .transaction_blob()) ==
                transactions[3]->getSerializer().slice());

            BEAST_EXPECT(
                makeSlice(reply.transactions_list()
                              .transactions(3)
                              .metadata_blob()) ==
                metas[3]->getSerializer().slice());
            BEAST_EXPECT(reply.skiplist_included());

            BEAST_EXPECT(s.slice() == makeSlice(reply.ledger_header()));

            auto parent = env.app().getLedgerMaster().getLedgerBySeq(3);

            SHAMap::Delta differences;

            int maxDifferences = std::numeric_limits<int>::max();

            bool res = parent->stateMap().compare(
                ledger->stateMap(), differences, maxDifferences);
            BEAST_EXPECT(res);

            size_t idx = 0;

            for (auto& [k, v] : differences)
            {
                auto obj = reply.ledger_objects().objects(idx);
                BEAST_EXPECT(k == uint256::fromVoid(obj.key().data()));
                if (v.second)
                {
                    BEAST_EXPECT(v.second->slice() == makeSlice(obj.data()));
                }
                else
                    BEAST_EXPECT(obj.data().size() == 0);

                if (!(v.first && v.second))
                {
                    auto succ = ledger->stateMap().upper_bound(k);
                    auto pred = ledger->stateMap().lower_bound(k);

                    if (succ != ledger->stateMap().end())
                        BEAST_EXPECT(
                            succ->key() ==
                            uint256::fromVoid(obj.successor().data()));
                    else
                        BEAST_EXPECT(obj.successor().size() == 0);
                    if (pred != ledger->stateMap().end())
                        BEAST_EXPECT(
                            pred->key() ==
                            uint256::fromVoid(obj.predecessor().data()));
                    else
                        BEAST_EXPECT(obj.predecessor().size() == 0);
                }
                ++idx;
            }
        }

        // Delete an account

        env(noop(alice));

        std::uint32_t const ledgerCount{
            env.current()->seq() + 257 - env.seq(alice)};

        for (std::uint32_t i = 0; i < ledgerCount; ++i)
            env.close();

        auto const acctDelFee{drops(env.current()->fees().increment)};
        env(acctdelete(alice, bob), fee(acctDelFee));
        env.close();

        {
            auto [status, reply] =
                grpcLedger(env.closed()->seq(), true, true, true, true);

            BEAST_EXPECT(status.ok());
            BEAST_EXPECT(reply.validated());
            auto base =
                env.app().getLedgerMaster().getLedgerBySeq(env.closed()->seq());

            auto parent = env.app().getLedgerMaster().getLedgerBySeq(
                env.closed()->seq() - 1);

            SHAMap::Delta differences;

            int maxDifferences = std::numeric_limits<int>::max();

            bool res = parent->stateMap().compare(
                base->stateMap(), differences, maxDifferences);
            BEAST_EXPECT(res);

            size_t idx = 0;
            for (auto& [k, v] : differences)
            {
                auto obj = reply.ledger_objects().objects(idx);
                BEAST_EXPECT(k == uint256::fromVoid(obj.key().data()));
                if (v.second)
                {
                    BEAST_EXPECT(
                        v.second->slice() ==
                        makeSlice(reply.ledger_objects().objects(idx).data()));
                }
                else
                    BEAST_EXPECT(obj.data().size() == 0);
                if (!(v.first && v.second))
                {
                    auto succ = base->stateMap().upper_bound(k);
                    auto pred = base->stateMap().lower_bound(k);

                    if (succ != base->stateMap().end())
                        BEAST_EXPECT(
                            succ->key() ==
                            uint256::fromVoid(obj.successor().data()));
                    else
                        BEAST_EXPECT(obj.successor().size() == 0);
                    if (pred != base->stateMap().end())
                        BEAST_EXPECT(
                            pred->key() ==
                            uint256::fromVoid(obj.predecessor().data()));
                    else
                        BEAST_EXPECT(obj.predecessor().size() == 0);
                }

                ++idx;
            }
        }
    }

    // gRPC stuff
    class GrpcLedgerDataClient : public GRPCTestClientBase
    {
    public:
        org::xrpl::rpc::v1::GetLedgerDataRequest request;
        org::xrpl::rpc::v1::GetLedgerDataResponse reply;

        explicit GrpcLedgerDataClient(std::string const& port)
            : GRPCTestClientBase(port)
        {
        }

        void
        GetLedgerData()
        {
            status = stub_->GetLedgerData(&context, request, &reply);
        }
    };
    void
    testGetLedgerData()
    {
        testcase("GetLedgerData");
        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort =
            *(*config)[SECTION_PORT_GRPC].get<std::string>("port");
        Env env(*this, std::move(config));
        auto grpcLedgerData = [&grpcPort](
                                  auto sequence, std::string marker = "") {
            GrpcLedgerDataClient grpcClient{grpcPort};

            grpcClient.request.mutable_ledger()->set_sequence(sequence);
            if (marker.size())
            {
                grpcClient.request.set_marker(marker);
            }

            grpcClient.GetLedgerData();
            return std::make_pair(grpcClient.status, grpcClient.reply);
        };

        Account const alice{"alice"};
        env.fund(XRP(100000), alice);

        int num_accounts = 10;

        for (auto i = 0; i < num_accounts; i++)
        {
            Account const bob{std::string("bob") + std::to_string(i)};
            env.fund(XRP(1000), bob);
        }
        env.close();

        {
            auto [status, reply] = grpcLedgerData(env.closed()->seq());
            BEAST_EXPECT(status.ok());

            BEAST_EXPECT(
                reply.ledger_objects().objects_size() == num_accounts + 4);
            BEAST_EXPECT(reply.marker().size() == 0);
            auto ledger = env.closed();
            size_t idx = 0;
            for (auto& sle : ledger->sles)
            {
                BEAST_EXPECT(
                    sle->getSerializer().slice() ==
                    makeSlice(reply.ledger_objects().objects(idx).data()));
                ++idx;
            }
        }

        {
            auto [status, reply] =
                grpcLedgerData(env.closed()->seq(), "bad marker");
            BEAST_EXPECT(!status.ok());
            BEAST_EXPECT(
                status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
        }

        num_accounts = 3000;

        for (auto i = 0; i < num_accounts; i++)
        {
            Account const cat{std::string("cat") + std::to_string(i)};
            env.fund(XRP(1000), cat);
            if (i % 100 == 0)
                env.close();
        }
        env.close();

        {
            auto [status, reply] = grpcLedgerData(env.closed()->seq());
            BEAST_EXPECT(status.ok());

            int maxLimit = RPC::Tuning::pageLength(true);
            BEAST_EXPECT(reply.ledger_objects().objects_size() == maxLimit);
            BEAST_EXPECT(reply.marker().size() != 0);

            auto [status2, reply2] =
                grpcLedgerData(env.closed()->seq(), reply.marker());
            BEAST_EXPECT(status2.ok());
            BEAST_EXPECT(reply2.marker().size() == 0);

            auto ledger = env.closed();
            size_t idx = 0;
            for (auto& sle : ledger->sles)
            {
                auto& obj = idx < maxLimit
                    ? reply.ledger_objects().objects(idx)
                    : reply2.ledger_objects().objects(idx - maxLimit);

                BEAST_EXPECT(
                    sle->getSerializer().slice() == makeSlice(obj.data()));
                ++idx;
            }
            BEAST_EXPECT(
                idx ==
                reply.ledger_objects().objects_size() +
                    reply2.ledger_objects().objects_size());
        }
    }

    // gRPC stuff
    class GrpcLedgerDiffClient : public GRPCTestClientBase
    {
    public:
        org::xrpl::rpc::v1::GetLedgerDiffRequest request;
        org::xrpl::rpc::v1::GetLedgerDiffResponse reply;

        explicit GrpcLedgerDiffClient(std::string const& port)
            : GRPCTestClientBase(port)
        {
        }

        void
        GetLedgerDiff()
        {
            status = stub_->GetLedgerDiff(&context, request, &reply);
        }
    };

    void
    testGetLedgerDiff()
    {
        testcase("GetLedgerDiff");
        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort =
            *(*config)[SECTION_PORT_GRPC].get<std::string>("port");
        Env env(*this, std::move(config));

        auto grpcLedgerDiff = [&grpcPort](
                                  auto baseSequence, auto desiredSequence) {
            GrpcLedgerDiffClient grpcClient{grpcPort};

            grpcClient.request.mutable_base_ledger()->set_sequence(
                baseSequence);
            grpcClient.request.mutable_desired_ledger()->set_sequence(
                desiredSequence);
            grpcClient.request.set_include_blobs(true);

            grpcClient.GetLedgerDiff();
            return std::make_pair(grpcClient.status, grpcClient.reply);
        };

        int num_accounts = 20;
        for (auto i = 0; i < num_accounts; i++)
        {
            Account const cat{std::string("cat") + std::to_string(i)};
            env.fund(XRP(1000), cat);
            if (i % 2 == 0)
                env.close();
        }
        env.close();

        auto compareDiffs = [&](auto baseSequence, auto desiredSequence) {
            auto [status, reply] =
                grpcLedgerDiff(baseSequence, desiredSequence);

            BEAST_EXPECT(status.ok());
            auto desired =
                env.app().getLedgerMaster().getLedgerBySeq(desiredSequence);

            auto base =
                env.app().getLedgerMaster().getLedgerBySeq(baseSequence);

            SHAMap::Delta differences;

            int maxDifferences = std::numeric_limits<int>::max();

            bool res = base->stateMap().compare(
                desired->stateMap(), differences, maxDifferences);
            if (!BEAST_EXPECT(res))
                return false;

            size_t idx = 0;
            for (auto& [k, v] : differences)
            {
                if (!BEAST_EXPECT(
                        k ==
                        uint256::fromVoid(
                            reply.ledger_objects().objects(idx).key().data())))
                    return false;
                if (v.second)
                {
                    if (!BEAST_EXPECT(
                            v.second->slice() ==
                            makeSlice(
                                reply.ledger_objects().objects(idx).data())))
                        return false;
                }

                ++idx;
            }
            return true;
        };

        // Adjacent ledgers
        BEAST_EXPECT(
            compareDiffs(env.closed()->seq() - 1, env.closed()->seq()));

        // Adjacent ledgers further in the past
        BEAST_EXPECT(
            compareDiffs(env.closed()->seq() - 3, env.closed()->seq() - 2));

        // Non-adjacent ledgers
        BEAST_EXPECT(
            compareDiffs(env.closed()->seq() - 5, env.closed()->seq() - 1));

        // Adjacent ledgers but in reverse order
        BEAST_EXPECT(
            compareDiffs(env.closed()->seq(), env.closed()->seq() - 1));

        // Non-adjacent ledgers in reverse order
        BEAST_EXPECT(
            compareDiffs(env.closed()->seq() - 1, env.closed()->seq() - 5));
    }

    // gRPC stuff
    class GrpcLedgerEntryClient : public GRPCTestClientBase
    {
    public:
        org::xrpl::rpc::v1::GetLedgerEntryRequest request;
        org::xrpl::rpc::v1::GetLedgerEntryResponse reply;

        explicit GrpcLedgerEntryClient(std::string const& port)
            : GRPCTestClientBase(port)
        {
        }

        void
        GetLedgerEntry()
        {
            status = stub_->GetLedgerEntry(&context, request, &reply);
        }
    };

    void
    testGetLedgerEntry()
    {
        testcase("GetLedgerDiff");
        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort =
            *(*config)[SECTION_PORT_GRPC].get<std::string>("port");
        Env env(*this, std::move(config));

        auto grpcLedgerEntry = [&grpcPort](auto sequence, auto key) {
            GrpcLedgerEntryClient grpcClient{grpcPort};

            grpcClient.request.mutable_ledger()->set_sequence(sequence);
            grpcClient.request.set_key(key.data(), key.size());

            grpcClient.GetLedgerEntry();
            return std::make_pair(grpcClient.status, grpcClient.reply);
        };

        Account const alice{"alice"};
        env.fund(XRP(1000), alice);
        env.close();

        for (auto& sle : env.closed()->sles)
        {
            auto [status, reply] =
                grpcLedgerEntry(env.closed()->seq(), sle->key());

            BEAST_EXPECT(status.ok());

            BEAST_EXPECT(
                uint256::fromVoid(reply.ledger_object().key().data()) ==
                sle->key());
            BEAST_EXPECT(
                makeSlice(reply.ledger_object().data()) ==
                sle->getSerializer().slice());
        }
    }

    void
    testNeedCurrentOrClosed()
    {
        testcase("NeedCurrentOrClosed");

        {
            org::xrpl::rpc::v1::GetLedgerRequest request;
            request.mutable_ledger()->set_sequence(1);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_hash("");
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_UNSPECIFIED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT);
            BEAST_EXPECT(needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED);
            BEAST_EXPECT(needCurrentOrClosed(request));
        }

        {
            org::xrpl::rpc::v1::GetLedgerDataRequest request;
            request.mutable_ledger()->set_sequence(1);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_hash("");
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_UNSPECIFIED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT);
            BEAST_EXPECT(needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED);
            BEAST_EXPECT(needCurrentOrClosed(request));
        }

        {
            org::xrpl::rpc::v1::GetLedgerEntryRequest request;
            request.mutable_ledger()->set_sequence(1);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_hash("");
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_UNSPECIFIED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT);
            BEAST_EXPECT(needCurrentOrClosed(request));
            request.mutable_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED);
            BEAST_EXPECT(needCurrentOrClosed(request));
        }

        {
            org::xrpl::rpc::v1::GetLedgerDiffRequest request;

            // set desired ledger, so desired ledger does not need current or
            // closed
            request.mutable_base_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED);

            request.mutable_base_ledger()->set_sequence(1);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_base_ledger()->set_hash("");
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_base_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_base_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_UNSPECIFIED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_base_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT);
            BEAST_EXPECT(needCurrentOrClosed(request));
            request.mutable_base_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED);
            BEAST_EXPECT(needCurrentOrClosed(request));

            // reset base ledger, so base ledger doesn't need current or closed
            request.mutable_base_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED);

            request.mutable_desired_ledger()->set_sequence(1);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_desired_ledger()->set_hash("");
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_desired_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_VALIDATED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_desired_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_UNSPECIFIED);
            BEAST_EXPECT(!needCurrentOrClosed(request));
            request.mutable_desired_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT);
            BEAST_EXPECT(needCurrentOrClosed(request));
            request.mutable_desired_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CLOSED);
            BEAST_EXPECT(needCurrentOrClosed(request));

            // both base and desired need current or closed
            request.mutable_base_ledger()->set_shortcut(
                org::xrpl::rpc::v1::LedgerSpecifier::SHORTCUT_CURRENT);
            BEAST_EXPECT(needCurrentOrClosed(request));
        }
    }

    void
    testSecureGateway()
    {
        testcase("SecureGateway");
        using namespace test::jtx;
        {
            std::unique_ptr<Config> config = envconfig(
                addGrpcConfigWithSecureGateway, getEnvLocalhostAddr());
            std::string grpcPort =
                *(*config)[SECTION_PORT_GRPC].get<std::string>("port");
            Env env(*this, std::move(config));

            env.close();

            auto ledger = env.app().getLedgerMaster().getLedgerBySeq(3);

            BEAST_EXPECT(env.current()->info().seq == 4);

            auto grpcLedger = [&grpcPort](
                                  auto sequence,
                                  std::string const& clientIp,
                                  std::string const& user) {
                GrpcLedgerClient grpcClient{grpcPort};

                grpcClient.request.mutable_ledger()->set_sequence(sequence);
                grpcClient.request.set_client_ip(clientIp);
                grpcClient.request.set_user(user);

                grpcClient.GetLedger();
                return std::make_pair(grpcClient.status, grpcClient.reply);
            };

            {
                auto [status, reply] =
                    grpcLedger(env.current()->info().seq, "", "");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedger(env.current()->info().seq, "", "ETL");
                BEAST_EXPECT(reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedger(env.current()->info().seq, "", "Reporting");
                BEAST_EXPECT(reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedger(env.current()->info().seq, "127.0.0.1", "ETL");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedger(env.current()->info().seq, "127.0.0.1", "");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
        }

        {
            std::string secureGatewayIp = "44.124.234.79";
            std::unique_ptr<Config> config =
                envconfig(addGrpcConfigWithSecureGateway, secureGatewayIp);
            std::string grpcPort =
                *(*config)[SECTION_PORT_GRPC].get<std::string>("port");
            Env env(*this, std::move(config));

            env.close();

            auto ledger = env.app().getLedgerMaster().getLedgerBySeq(3);

            BEAST_EXPECT(env.current()->info().seq == 4);

            auto grpcLedger = [&grpcPort](
                                  auto sequence,
                                  std::string const& clientIp,
                                  std::string const& user) {
                GrpcLedgerClient grpcClient{grpcPort};

                grpcClient.request.mutable_ledger()->set_sequence(sequence);
                grpcClient.request.set_client_ip(clientIp);
                grpcClient.request.set_user(user);

                grpcClient.GetLedger();
                return std::make_pair(grpcClient.status, grpcClient.reply);
            };

            {
                auto [status, reply] =
                    grpcLedger(env.current()->info().seq, "", "");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedger(env.current()->info().seq, "", "ETL");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] = grpcLedger(
                    env.current()->info().seq, secureGatewayIp, "ETL");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedger(env.current()->info().seq, secureGatewayIp, "");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
        }

        {
            std::unique_ptr<Config> config = envconfig(
                addGrpcConfigWithSecureGateway, getEnvLocalhostAddr());
            std::string grpcPort =
                *(*config)[SECTION_PORT_GRPC].get<std::string>("port");
            Env env(*this, std::move(config));

            env.close();

            auto ledger = env.app().getLedgerMaster().getLedgerBySeq(3);

            BEAST_EXPECT(env.current()->info().seq == 4);
            auto grpcLedgerData = [&grpcPort](
                                      auto sequence,
                                      std::string const& clientIp,
                                      std::string const& user) {
                GrpcLedgerDataClient grpcClient{grpcPort};

                grpcClient.request.mutable_ledger()->set_sequence(sequence);
                grpcClient.request.set_client_ip(clientIp);
                grpcClient.request.set_user(user);

                grpcClient.GetLedgerData();
                return std::make_pair(grpcClient.status, grpcClient.reply);
            };
            {
                auto [status, reply] =
                    grpcLedgerData(env.current()->info().seq, "", "");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedgerData(env.current()->info().seq, "", "ETL");
                BEAST_EXPECT(reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedgerData(env.current()->info().seq, "", "Reporting");
                BEAST_EXPECT(reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] = grpcLedgerData(
                    env.current()->info().seq, "127.0.0.1", "ETL");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedgerData(env.current()->info().seq, "127.0.0.1", "");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
        }
        {
            std::string secureGatewayIp = "44.124.234.79";
            std::unique_ptr<Config> config =
                envconfig(addGrpcConfigWithSecureGateway, secureGatewayIp);
            std::string grpcPort =
                *(*config)[SECTION_PORT_GRPC].get<std::string>("port");
            Env env(*this, std::move(config));

            env.close();

            auto ledger = env.app().getLedgerMaster().getLedgerBySeq(3);

            BEAST_EXPECT(env.current()->info().seq == 4);

            auto grpcLedgerData = [&grpcPort](
                                      auto sequence,
                                      std::string const& clientIp,
                                      std::string const& user) {
                GrpcLedgerDataClient grpcClient{grpcPort};

                grpcClient.request.mutable_ledger()->set_sequence(sequence);
                grpcClient.request.set_client_ip(clientIp);
                grpcClient.request.set_user(user);

                grpcClient.GetLedgerData();
                return std::make_pair(grpcClient.status, grpcClient.reply);
            };

            {
                auto [status, reply] =
                    grpcLedgerData(env.current()->info().seq, "", "");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] =
                    grpcLedgerData(env.current()->info().seq, "", "ETL");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] = grpcLedgerData(
                    env.current()->info().seq, secureGatewayIp, "ETL");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
            {
                auto [status, reply] = grpcLedgerData(
                    env.current()->info().seq, secureGatewayIp, "");
                BEAST_EXPECT(!reply.is_unlimited());
                BEAST_EXPECT(status.ok());
            }
        }
    }

public:
    void
    run() override
    {
        testGetLedger();

        testGetLedgerData();

        testGetLedgerDiff();

        testGetLedgerEntry();

        testNeedCurrentOrClosed();

        testSecureGateway();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(ReportingETL, app, ripple, 2);

}  // namespace test
}  // namespace ripple
