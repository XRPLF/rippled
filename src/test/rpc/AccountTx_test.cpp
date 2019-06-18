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
#include <test/jtx.h>

namespace ripple {

namespace test {

class AccountTx_test : public beast::unit_test::suite
{
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
                rpcLGR_IDXS_INVALID));
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
                rpcLGR_IDXS_INVALID));
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

            p[jss::ledger_index_max] = env.closed()->info().seq - 1 ;
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
                env.rpc("json", "account_tx", to_string(p)),
                rpcLGR_NOT_FOUND));
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

        Env env(*this);
        Account const alice {"alice"};
        Account const alie {"alie"};
        Account const gw {"gw"};
        auto const USD {gw["USD"]};

        env.fund(XRP(1000000), alice, gw);
        env.close();

        // AccountSet
        env (noop (alice));

        // Payment
        env (pay (alice, gw, XRP (100)));

        // Regular key set
        env (regkey(alice, alie));
        env.close();

        // Trust and Offers
        env (trust (alice, USD (200)), sig (alie));
        std::uint32_t const offerSeq {env.seq(alice)};
        env (offer (alice, USD (50), XRP (150)), sig (alie));
        env.close();

        {
            Json::Value cancelOffer;
            cancelOffer[jss::Account] = alice.human();
            cancelOffer[jss::OfferSequence] = offerSeq;
            cancelOffer[jss::TransactionType] = jss::OfferCancel;
            env (cancelOffer, sig (alie));
        }
        env.close();

        // SignerListSet
        env (signers (alice, 1, {{"bogie", 1}, {"demon", 1}}), sig (alie));

        // Escrow
        {
            // Create an escrow.  Requires either a CancelAfter or FinishAfter.
            auto escrow = [] (Account const& account,
                                  Account const& to, STAmount const& amount)
            {
                Json::Value escro;
                escro[jss::TransactionType] = jss::EscrowCreate;
                escro[jss::Flags] = tfUniversal;
                escro[jss::Account] = account.human();
                escro[jss::Destination] = to.human();
                escro[jss::Amount] = amount.getJson(JsonOptions::none);
                return escro;
            };

            NetClock::time_point const nextTime {env.now() + 2s};
            
            Json::Value escrowWithFinish {escrow (alice, alice, XRP (500))};
            escrowWithFinish[sfFinishAfter.jsonName] = 
                nextTime.time_since_epoch().count();

            std::uint32_t const escrowFinishSeq {env.seq(alice)};
            env (escrowWithFinish, sig (alie));

            Json::Value escrowWithCancel {escrow (alice, alice, XRP (500))};
            escrowWithCancel[sfFinishAfter.jsonName] =
                nextTime.time_since_epoch().count();
            escrowWithCancel[sfCancelAfter.jsonName] =
                nextTime.time_since_epoch().count() + 1;

            std::uint32_t const escrowCancelSeq {env.seq(alice)};
            env (escrowWithCancel, sig (alie));
            env.close();

            {
                Json::Value escrowFinish;
                escrowFinish[jss::TransactionType] = jss::EscrowFinish;
                escrowFinish[jss::Flags] = tfUniversal;
                escrowFinish[jss::Account] = alice.human();
                escrowFinish[sfOwner.jsonName] = alice.human();
                escrowFinish[sfOfferSequence.jsonName] = escrowFinishSeq;
                env (escrowFinish, sig (alie));
            }
            {
                Json::Value escrowCancel;
                escrowCancel[jss::TransactionType] = jss::EscrowCancel;
                escrowCancel[jss::Flags] = tfUniversal;
                escrowCancel[jss::Account] = alice.human();
                escrowCancel[sfOwner.jsonName] = alice.human();
                escrowCancel[sfOfferSequence.jsonName] = escrowCancelSeq;
                env (escrowCancel, sig (alie));
            }
            env.close();
        }

        // PayChan
        {
            std::uint32_t payChanSeq {env.seq (alice)};
            Json::Value payChanCreate;
            payChanCreate[jss::TransactionType] = jss::PaymentChannelCreate;
            payChanCreate[jss::Flags] = tfUniversal;
            payChanCreate[jss::Account] = alice.human();
            payChanCreate[jss::Destination] = gw.human();
            payChanCreate[jss::Amount] =
                XRP (500).value().getJson (JsonOptions::none);
            payChanCreate[sfSettleDelay.jsonName] =
                NetClock::duration{100s}.count();
            payChanCreate[sfPublicKey.jsonName] = strHex (alice.pk().slice());
            env (payChanCreate, sig (alie));
            env.close();
            
            std::string const payChanIndex {
                strHex (keylet::payChan (alice, gw, payChanSeq).key)};
            
            {
                Json::Value payChanFund;
                payChanFund[jss::TransactionType] = jss::PaymentChannelFund;
                payChanFund[jss::Flags] = tfUniversal;
                payChanFund[jss::Account] = alice.human();
                payChanFund[sfPayChannel.jsonName] = payChanIndex;
                payChanFund[jss::Amount] =
                    XRP (200).value().getJson (JsonOptions::none);
                env (payChanFund, sig (alie));
                env.close();
            }
            {
                Json::Value payChanClaim;
                payChanClaim[jss::TransactionType] = jss::PaymentChannelClaim;
                payChanClaim[jss::Flags] = tfClose;
                payChanClaim[jss::Account] = gw.human();
                payChanClaim[sfPayChannel.jsonName] = payChanIndex;
                payChanClaim[sfPublicKey.jsonName] = strHex(alice.pk().slice());
                env (payChanClaim);
                env.close();
            }
        }

        // Check
        {
            uint256 const aliceCheckId {
                getCheckIndex (alice, env.seq (alice))};
            env (check::create (alice, gw, XRP (300)), sig (alie));

            uint256 const gwCheckId {
                getCheckIndex (gw, env.seq (gw))};
            env (check::create (gw, alice, XRP (200)));
            env.close();

            env (check::cash (alice, gwCheckId, XRP (200)), sig (alie));
            env (check::cancel (alice, aliceCheckId), sig (alie));
            env.close();
        }

        // Deposit preauthorization.
        env (deposit::auth (alice, gw), sig (alie));
        env.close();

        // Setup is done.  Look at the transactions returned by account_tx.
        Json::Value params;
        params[jss::account] = alice.human();
        params[jss::ledger_index_min] = -1;
        params[jss::ledger_index_max] = -1;

        Json::Value const result {
            env.rpc("json", "account_tx", to_string(params))};

        BEAST_EXPECT (result[jss::result][jss::status] == "success");
        BEAST_EXPECT (result[jss::result][jss::transactions].isArray());

        Json::Value const& txs {result[jss::result][jss::transactions]};

        // Do a sanity check on each returned transaction.  They should
        // be returned in the reverse order of application to the ledger.
        struct NodeSanity
        {
            int const index;
            Json::StaticString const& txType;
            std::initializer_list<char const*> created;
            std::initializer_list<char const*> deleted;
            std::initializer_list<char const*> modified;
        };

       auto checkSanity = [this] (
            Json::Value const& txNode, NodeSanity const& sane)
        {
            BEAST_EXPECT(txNode[jss::validated].asBool() == true);
            BEAST_EXPECT(
                txNode[jss::tx][sfTransactionType.jsonName].asString() == 
                sane.txType);

            // Make sure all of the expected node types are present.
            std::vector<std::string> createdNodes {};
            std::vector<std::string> deletedNodes {};
            std::vector<std::string> modifiedNodes{};

            for (Json::Value const& metaNode :
                txNode[jss::meta][sfAffectedNodes.jsonName])
            {
                if (metaNode.isMember (sfCreatedNode.jsonName))
                    createdNodes.push_back (
                        metaNode[sfCreatedNode.jsonName]
                            [sfLedgerEntryType.jsonName].asString());

                else if (metaNode.isMember (sfDeletedNode.jsonName))
                    deletedNodes.push_back (
                        metaNode[sfDeletedNode.jsonName]
                            [sfLedgerEntryType.jsonName].asString());

                else if (metaNode.isMember (sfModifiedNode.jsonName))
                    modifiedNodes.push_back (
                        metaNode[sfModifiedNode.jsonName]
                            [sfLedgerEntryType.jsonName].asString());

                else
                    fail ("Unexpected or unlabeled node type in metadata.",
                        __FILE__, __LINE__);
            }

            auto cmpNodeTypes = [this] (
                char const* const errMsg,
                std::vector<std::string>& got,
                std::initializer_list<char const*> expList)
            {
                std::sort (got.begin(), got.end());

                std::vector<std::string> exp;
                exp.reserve (expList.size());
                for (char const* nodeType : expList)
                    exp.push_back (nodeType);
                std::sort (exp.begin(), exp.end());

                if (got != exp)
                {
                    fail (errMsg, __FILE__, __LINE__);
                }
            };

            cmpNodeTypes ("Created mismatch", createdNodes, sane.created);
            cmpNodeTypes ("Deleted mismatch", deletedNodes, sane.deleted);
            cmpNodeTypes ("Modified mismatch", modifiedNodes, sane.modified);
        };

        static const NodeSanity sanity[]
        {
            //    txType,                    created,                                                    deleted,                          modified
            {  0, jss::DepositPreauth,       {jss::DepositPreauth},                                      {},                               {jss::AccountRoot, jss::DirectoryNode}},
            {  1, jss::CheckCancel,          {},                                                         {jss::Check},                     {jss::AccountRoot, jss::DirectoryNode, jss::DirectoryNode}},
            {  2, jss::CheckCash,            {},                                                         {jss::Check},                     {jss::AccountRoot, jss::AccountRoot,   jss::DirectoryNode, jss::DirectoryNode}},
            {  3, jss::CheckCreate,          {jss::Check},                                               {},                               {jss::AccountRoot, jss::DirectoryNode, jss::DirectoryNode}},
            {  4, jss::CheckCreate,          {jss::Check},                                               {},                               {jss::AccountRoot, jss::DirectoryNode, jss::DirectoryNode}},
            {  5, jss::PaymentChannelClaim,  {},                                                         {jss::PayChannel},                {jss::AccountRoot, jss::AccountRoot,   jss::DirectoryNode}},
            {  6, jss::PaymentChannelFund,   {},                                                         {},                               {jss::AccountRoot, jss::PayChannel   }},
            {  7, jss::PaymentChannelCreate, {jss::PayChannel},                                          {},                               {jss::AccountRoot, jss::AccountRoot,   jss::DirectoryNode}},
            {  8, jss::EscrowCancel,         {},                                                         {jss::Escrow},                    {jss::AccountRoot, jss::DirectoryNode}},
            {  9, jss::EscrowFinish,         {},                                                         {jss::Escrow},                    {jss::AccountRoot, jss::DirectoryNode}},
            { 10, jss::EscrowCreate,         {jss::Escrow},                                              {},                               {jss::AccountRoot, jss::DirectoryNode}},
            { 11, jss::EscrowCreate,         {jss::Escrow},                                              {},                               {jss::AccountRoot, jss::DirectoryNode}},
            { 12, jss::SignerListSet,        {jss::SignerList},                                          {},                               {jss::AccountRoot, jss::DirectoryNode}},
            { 13, jss::OfferCancel,          {},                                                         {jss::Offer, jss::DirectoryNode}, {jss::AccountRoot, jss::DirectoryNode}},
            { 14, jss::OfferCreate,          {jss::Offer, jss::DirectoryNode},                           {},                               {jss::AccountRoot, jss::DirectoryNode}},
            { 15, jss::TrustSet,             {jss::RippleState, jss::DirectoryNode, jss::DirectoryNode}, {},                               {jss::AccountRoot, jss::AccountRoot}},
            { 16, jss::SetRegularKey,        {},                                                         {},                               {jss::AccountRoot}},
            { 17, jss::Payment,              {},                                                         {},                               {jss::AccountRoot, jss::AccountRoot}},
            { 18, jss::AccountSet,           {},                                                         {},                               {jss::AccountRoot}},
            { 19, jss::AccountSet,           {},                                                         {},                               {jss::AccountRoot}},
            { 20, jss::Payment,              {jss::AccountRoot},                                         {},                               {jss::AccountRoot}},
        };

        BEAST_EXPECT (std::extent<decltype (sanity)>::value ==
            result[jss::result][jss::transactions].size());

        for (unsigned int index {0};
            index < std::extent<decltype (sanity)>::value; ++index)
        {
            checkSanity (txs[index], sanity[index]);
        }
    }

public:
    void
    run() override
    {
        testParameters();
        testContents();
    }
};
BEAST_DEFINE_TESTSUITE(AccountTx, app, ripple);

}  // namespace test
}  // namespace ripple
