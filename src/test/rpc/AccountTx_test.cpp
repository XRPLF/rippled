//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>

#include <boost/container/flat_set.hpp>

namespace ripple {

namespace test {

class AccountTx_test : public beast::unit_test::suite
{
    // A data structure used to describe the basic structure of a
    // transactions array node as returned by the account_tx RPC command.
    struct NodeSanity
    {
        int const index;
        Json::StaticString const& txType;
        boost::container::flat_set<std::string> created;
        boost::container::flat_set<std::string> deleted;
        boost::container::flat_set<std::string> modified;

        NodeSanity(
            int idx,
            Json::StaticString const& t,
            std::initializer_list<char const*> c,
            std::initializer_list<char const*> d,
            std::initializer_list<char const*> m)
            : index(idx), txType(t)
        {
            auto buildSet = [](auto&& init) {
                boost::container::flat_set<std::string> r;
                r.reserve(init.size());
                for (auto&& s : init)
                    r.insert(s);
                return r;
            };

            created = buildSet(c);
            deleted = buildSet(d);
            modified = buildSet(m);
        }
    };

    // A helper method tests can use to validate returned JSON vs NodeSanity.
    void
    checkSanity(Json::Value const& txNode, NodeSanity const& sane)
    {
        BEAST_EXPECT(txNode[jss::validated].asBool() == true);
        BEAST_EXPECT(
            txNode[jss::tx][sfTransactionType.jsonName].asString() ==
            sane.txType);

        // Make sure all of the expected node types are present.
        boost::container::flat_set<std::string> createdNodes;
        boost::container::flat_set<std::string> deletedNodes;
        boost::container::flat_set<std::string> modifiedNodes;

        for (Json::Value const& metaNode :
             txNode[jss::meta][sfAffectedNodes.jsonName])
        {
            if (metaNode.isMember(sfCreatedNode.jsonName))
                createdNodes.insert(
                    metaNode[sfCreatedNode.jsonName][sfLedgerEntryType.jsonName]
                        .asString());

            else if (metaNode.isMember(sfDeletedNode.jsonName))
                deletedNodes.insert(
                    metaNode[sfDeletedNode.jsonName][sfLedgerEntryType.jsonName]
                        .asString());

            else if (metaNode.isMember(sfModifiedNode.jsonName))
                modifiedNodes.insert(metaNode[sfModifiedNode.jsonName]
                                             [sfLedgerEntryType.jsonName]
                                                 .asString());

            else
                fail(
                    "Unexpected or unlabeled node type in metadata.",
                    __FILE__,
                    __LINE__);
        }

        BEAST_EXPECT(createdNodes == sane.created);
        BEAST_EXPECT(deletedNodes == sane.deleted);
        BEAST_EXPECT(modifiedNodes == sane.modified);
    };

    void
    testParameters()
    {
        using namespace test::jtx;

        Env env(*this);
        Account A1{"A1"};
        env.fund(XRP(10000), A1);
        env.close();

        // Ledger 3 has the two txs associated with funding the account
        // All other ledgers have no txs

        auto hasTxs = [](Json::Value const& j) {
            return j.isMember(jss::result) &&
                (j[jss::result][jss::status] == "success") &&
                (j[jss::result][jss::transactions].size() == 2) &&
                (j[jss::result][jss::transactions][0u][jss::tx]
                  [jss::TransactionType] == jss::AccountSet) &&
                (j[jss::result][jss::transactions][1u][jss::tx]
                  [jss::TransactionType] == jss::Payment);
        };

        auto noTxs = [](Json::Value const& j) {
            return j.isMember(jss::result) &&
                (j[jss::result][jss::status] == "success") &&
                (j[jss::result][jss::transactions].size() == 0);
        };

        auto isErr = [](Json::Value const& j, error_code_i code) {
            return j.isMember(jss::result) &&
                j[jss::result].isMember(jss::error) &&
                j[jss::result][jss::error] == RPC::get_error_info(code).token;
        };

        Json::Value jParms;

        BEAST_EXPECT(isErr(
            env.rpc("json", "account_tx", to_string(jParms)),
            rpcINVALID_PARAMS));

        jParms[jss::account] = "0xDEADBEEF";

        BEAST_EXPECT(isErr(
            env.rpc("json", "account_tx", to_string(jParms)),
            rpcACT_MALFORMED));

        jParms[jss::account] = A1.human();
        BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(jParms))));

        // Ledger min/max index
        {
            Json::Value p{jParms};
            p[jss::ledger_index_min] = -1;
            p[jss::ledger_index_max] = -1;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = 0;
            p[jss::ledger_index_max] = 100;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = 1;
            p[jss::ledger_index_max] = 2;
            BEAST_EXPECT(noTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = 2;
            p[jss::ledger_index_max] = 1;
            BEAST_EXPECT(isErr(
                env.rpc("json", "account_tx", to_string(p)),
                (RPC::ApiMaximumSupportedVersion == 1 ? rpcLGR_IDXS_INVALID
                                                      : rpcINVALID_LGR_RANGE)));
        }

        // Ledger index min only
        {
            Json::Value p{jParms};
            p[jss::ledger_index_min] = -1;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = 1;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = env.current()->info().seq;
            BEAST_EXPECT(isErr(
                env.rpc("json", "account_tx", to_string(p)),
                (RPC::ApiMaximumSupportedVersion == 1 ? rpcLGR_IDXS_INVALID
                                                      : rpcINVALID_LGR_RANGE)));
        }

        // Ledger index max only
        {
            Json::Value p{jParms};
            p[jss::ledger_index_max] = -1;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_max] = env.current()->info().seq;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_max] = env.closed()->info().seq;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_max] = env.closed()->info().seq - 1;
            BEAST_EXPECT(noTxs(env.rpc("json", "account_tx", to_string(p))));
        }

        // Ledger Sequence
        {
            Json::Value p{jParms};

            p[jss::ledger_index] = env.closed()->info().seq;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index] = env.closed()->info().seq - 1;
            BEAST_EXPECT(noTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index] = env.current()->info().seq;
            BEAST_EXPECT(isErr(
                env.rpc("json", "account_tx", to_string(p)),
                rpcLGR_NOT_VALIDATED));

            p[jss::ledger_index] = env.current()->info().seq + 1;
            BEAST_EXPECT(isErr(
                env.rpc("json", "account_tx", to_string(p)), rpcLGR_NOT_FOUND));
        }

        // Ledger Hash
        {
            Json::Value p{jParms};

            p[jss::ledger_hash] = to_string(env.closed()->info().hash);
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_hash] = to_string(env.closed()->info().parentHash);
            BEAST_EXPECT(noTxs(env.rpc("json", "account_tx", to_string(p))));
        }
    }

    void
    testContents()
    {
        // Get results for all transaction types that can be associated
        // with an account.  Start by generating all transaction types.
        using namespace test::jtx;
        using namespace std::chrono_literals;

        Env env(*this, supported_amendments() | featureTicketBatch);
        Account const alice{"alice"};
        Account const alie{"alie"};
        Account const gw{"gw"};
        auto const USD{gw["USD"]};

        env.fund(XRP(1000000), alice, gw);
        env.close();

        // AccountSet
        env(noop(alice));

        // Payment
        env(pay(alice, gw, XRP(100)));

        // Regular key set
        env(regkey(alice, alie));
        env.close();

        // Trust and Offers
        env(trust(alice, USD(200)), sig(alie));
        std::uint32_t const offerSeq{env.seq(alice)};
        env(offer(alice, USD(50), XRP(150)), sig(alie));
        env.close();

        {
            Json::Value cancelOffer;
            cancelOffer[jss::Account] = alice.human();
            cancelOffer[jss::OfferSequence] = offerSeq;
            cancelOffer[jss::TransactionType] = jss::OfferCancel;
            env(cancelOffer, sig(alie));
        }
        env.close();

        // SignerListSet
        env(signers(alice, 1, {{"bogie", 1}, {"demon", 1}}), sig(alie));

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

            Json::Value escrowWithCancel{escrow(alice, alice, XRP(500))};
            escrowWithCancel[sfFinishAfter.jsonName] =
                nextTime.time_since_epoch().count();
            escrowWithCancel[sfCancelAfter.jsonName] =
                nextTime.time_since_epoch().count() + 1;

            std::uint32_t const escrowCancelSeq{env.seq(alice)};
            env(escrowWithCancel, sig(alie));
            env.close();

            {
                Json::Value escrowFinish;
                escrowFinish[jss::TransactionType] = jss::EscrowFinish;
                escrowFinish[jss::Flags] = tfUniversal;
                escrowFinish[jss::Account] = alice.human();
                escrowFinish[sfOwner.jsonName] = alice.human();
                escrowFinish[sfOfferSequence.jsonName] = escrowFinishSeq;
                env(escrowFinish, sig(alie));
            }
            {
                Json::Value escrowCancel;
                escrowCancel[jss::TransactionType] = jss::EscrowCancel;
                escrowCancel[jss::Flags] = tfUniversal;
                escrowCancel[jss::Account] = alice.human();
                escrowCancel[sfOwner.jsonName] = alice.human();
                escrowCancel[sfOfferSequence.jsonName] = escrowCancelSeq;
                env(escrowCancel, sig(alie));
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
            }
        }

        // Check
        {
            auto const aliceCheckId = keylet::check(alice, env.seq(alice)).key;
            env(check::create(alice, gw, XRP(300)), sig(alie));

            auto const gwCheckId = keylet::check(gw, env.seq(gw)).key;
            env(check::create(gw, alice, XRP(200)));
            env.close();

            env(check::cash(alice, gwCheckId, XRP(200)), sig(alie));
            env(check::cancel(alice, aliceCheckId), sig(alie));
            env.close();
        }
        {
            // Deposit preauthorization with a Ticket.
            std::uint32_t const tktSeq{env.seq(alice) + 1};
            env(ticket::create(alice, 1), sig(alie));
            env.close();

            env(deposit::auth(alice, gw), ticket::use(tktSeq), sig(alie));
            env.close();
        }

        // Setup is done.  Look at the transactions returned by account_tx.
        Json::Value params;
        params[jss::account] = alice.human();
        params[jss::ledger_index_min] = -1;
        params[jss::ledger_index_max] = -1;

        Json::Value const result{
            env.rpc("json", "account_tx", to_string(params))};

        BEAST_EXPECT(result[jss::result][jss::status] == "success");
        BEAST_EXPECT(result[jss::result][jss::transactions].isArray());

        Json::Value const& txs{result[jss::result][jss::transactions]};

        // clang-format off
        // Do a sanity check on each returned transaction.  They should
        // be returned in the reverse order of application to the ledger.
        static const NodeSanity sanity[]{
            //    txType,                    created,                                                    deleted,                          modified
            {0,  jss::DepositPreauth,         {jss::DepositPreauth},                                      {jss::Ticket},                    {jss::AccountRoot, jss::DirectoryNode}},
            {1,  jss::TicketCreate,           {jss::Ticket},                                              {},                               {jss::AccountRoot, jss::DirectoryNode}},
            {2,  jss::CheckCancel,            {},                                                         {jss::Check},                     {jss::AccountRoot, jss::AccountRoot, jss::DirectoryNode, jss::DirectoryNode}},
            {3,  jss::CheckCash,              {},                                                         {jss::Check},                     {jss::AccountRoot, jss::AccountRoot, jss::DirectoryNode, jss::DirectoryNode}},
            {4,  jss::CheckCreate,            {jss::Check},                                               {},                               {jss::AccountRoot, jss::AccountRoot, jss::DirectoryNode, jss::DirectoryNode}},
            {5,  jss::CheckCreate,            {jss::Check},                                               {},                               {jss::AccountRoot, jss::AccountRoot, jss::DirectoryNode, jss::DirectoryNode}},
            {6,  jss::PaymentChannelClaim,    {},                                                         {jss::PayChannel},                {jss::AccountRoot, jss::AccountRoot, jss::DirectoryNode, jss::DirectoryNode}},
            {7,  jss::PaymentChannelFund,     {},                                                         {},                               {jss::AccountRoot, jss::PayChannel}},
            {8,  jss::PaymentChannelCreate,   {jss::PayChannel},                                          {},                               {jss::AccountRoot, jss::AccountRoot, jss::DirectoryNode, jss::DirectoryNode}},
            {9,  jss::EscrowCancel,           {},                                                         {jss::Escrow},                    {jss::AccountRoot, jss::DirectoryNode}},
            {10, jss::EscrowFinish,           {},                                                         {jss::Escrow},                    {jss::AccountRoot, jss::DirectoryNode}},
            {11, jss::EscrowCreate,           {jss::Escrow},                                              {},                               {jss::AccountRoot, jss::DirectoryNode}},
            {12, jss::EscrowCreate,           {jss::Escrow},                                              {},                               {jss::AccountRoot, jss::DirectoryNode}},
            {13, jss::SignerListSet,          {jss::SignerList},                                          {},                               {jss::AccountRoot, jss::DirectoryNode}},
            {14, jss::OfferCancel,            {},                                                         {jss::Offer, jss::DirectoryNode}, {jss::AccountRoot, jss::DirectoryNode}},
            {15, jss::OfferCreate,            {jss::Offer, jss::DirectoryNode},                           {},                               {jss::AccountRoot, jss::DirectoryNode}},
            {16, jss::TrustSet,               {jss::RippleState, jss::DirectoryNode, jss::DirectoryNode}, {},                               {jss::AccountRoot, jss::AccountRoot}},
            {17, jss::SetRegularKey,          {},                                                         {},                               {jss::AccountRoot}},
            {18, jss::Payment,                {},                                                         {},                               {jss::AccountRoot, jss::AccountRoot}},
            {19, jss::AccountSet,             {},                                                         {},                               {jss::AccountRoot}},
            {20, jss::AccountSet,             {},                                                         {},                               {jss::AccountRoot}},
            {21, jss::Payment,                {jss::AccountRoot},                                         {},                               {jss::AccountRoot}},
        };
        // clang-format on

        BEAST_EXPECT(
            std::size(sanity) == result[jss::result][jss::transactions].size());

        for (unsigned int index{0}; index < std::size(sanity); ++index)
        {
            checkSanity(txs[index], sanity[index]);
        }
    }

    void
    testAccountDelete()
    {
        // Verify that if an account is resurrected then the account_tx RPC
        // command still recovers all transactions on that account before
        // and after resurrection.
        using namespace test::jtx;
        using namespace std::chrono_literals;

        Env env(*this);
        Account const alice{"alice"};
        Account const becky{"becky"};

        env.fund(XRP(10000), alice, becky);
        env.close();

        // Verify that becky's account root is present.
        Keylet const beckyAcctKey{keylet::account(becky.id())};
        BEAST_EXPECT(env.closed()->exists(beckyAcctKey));

        // becky does an AccountSet .
        env(noop(becky));

        // Close enough ledgers to be able to delete becky's account.
        std::uint32_t const ledgerCount{
            env.current()->seq() + 257 - env.seq(becky)};

        for (std::uint32_t i = 0; i < ledgerCount; ++i)
            env.close();

        auto const beckyPreDelBalance{env.balance(becky)};

        auto const acctDelFee{drops(env.current()->fees().increment)};
        env(acctdelete(becky, alice), fee(acctDelFee));
        env.close();

        // Verify that becky's account root is gone.
        BEAST_EXPECT(!env.closed()->exists(beckyAcctKey));
        env.close();

        // clang-format off
        // Do a sanity check on each returned transaction.  They should
        // be returned in the reverse order of application to the ledger.
        //
        // Note that the first two transactions in sanity have not occurred
        // yet.  We'll see those after becky's account is resurrected.
        static const NodeSanity sanity[]
        {
                                    //   txType,                    created,            deleted,            modified
/* becky pays alice              */ { 0, jss::Payment,              {},                 {},                 {jss::AccountRoot, jss::AccountRoot}},
/* alice resurrects becky's acct */ { 1, jss::Payment,              {jss::AccountRoot}, {},                 {jss::AccountRoot}},
/* becky deletes her account     */ { 2, jss::AccountDelete,        {},                 {jss::AccountRoot}, {jss::AccountRoot}},
/* becky's noop                  */ { 3, jss::AccountSet,           {},                 {},                 {jss::AccountRoot}},
/* "fund" sets flags             */ { 4, jss::AccountSet,           {},                 {},                 {jss::AccountRoot}},
/* "fund" creates becky's acct   */ { 5, jss::Payment,              {jss::AccountRoot}, {},                 {jss::AccountRoot}}
        };
        // clang-format on

        // Verify that we can recover becky's account_tx information even
        // after the account is deleted.
        {
            Json::Value params;
            params[jss::account] = becky.human();
            params[jss::ledger_index_min] = -1;
            params[jss::ledger_index_max] = -1;

            Json::Value const result{
                env.rpc("json", "account_tx", to_string(params))};

            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result][jss::transactions].isArray());

            // The first two transactions listed in sanity haven't happened yet.
            constexpr unsigned int beckyDeletedOffest = 2;
            BEAST_EXPECT(
                std::size(sanity) ==
                result[jss::result][jss::transactions].size() +
                    beckyDeletedOffest);

            Json::Value const& txs{result[jss::result][jss::transactions]};

            for (unsigned int index = beckyDeletedOffest;
                 index < std::size(sanity);
                 ++index)
            {
                checkSanity(txs[index - beckyDeletedOffest], sanity[index]);
            }
        }

        // All it takes is a large enough XRP payment to resurrect
        // becky's account.  Try too small a payment.
        env(pay(alice, becky, XRP(19)), ter(tecNO_DST_INSUF_XRP));
        env.close();

        // Actually resurrect becky's account.
        env(pay(alice, becky, XRP(45)));
        env.close();

        // becky's account root should be back.
        BEAST_EXPECT(env.closed()->exists(beckyAcctKey));
        BEAST_EXPECT(env.balance(becky) == XRP(45));

        // becky pays alice.
        env(pay(becky, alice, XRP(20)));
        env.close();

        // Setup is done.  Look at the transactions returned by account_tx.
        // Verify that account_tx locates all of becky's transactions.
        Json::Value params;
        params[jss::account] = becky.human();
        params[jss::ledger_index_min] = -1;
        params[jss::ledger_index_max] = -1;

        Json::Value const result{
            env.rpc("json", "account_tx", to_string(params))};

        BEAST_EXPECT(result[jss::result][jss::status] == "success");
        BEAST_EXPECT(result[jss::result][jss::transactions].isArray());

        BEAST_EXPECT(
            std::size(sanity) == result[jss::result][jss::transactions].size());

        Json::Value const& txs{result[jss::result][jss::transactions]};

        for (unsigned int index = 0; index < std::size(sanity); ++index)
        {
            checkSanity(txs[index], sanity[index]);
        }
    }

public:
    void
    run() override
    {
        testParameters();
        testContents();
        testAccountDelete();
    }
};
BEAST_DEFINE_TESTSUITE(AccountTx, app, ripple);

}  // namespace test
}  // namespace ripple
