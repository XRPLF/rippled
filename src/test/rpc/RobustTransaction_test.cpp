#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include <xrpld/core/JobQueue.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class RobustTransaction_test : public beast::unit_test::suite
{
public:
    void
    testSequenceRealignment()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");
        env.close();
        auto wsc = makeWSClient(env.app().config());

        {
            // RPC subscribe to transactions stream
            Json::Value jv;
            jv[jss::streams] = Json::arrayValue;
            jv[jss::streams].append("transactions");
            jv = wsc->invoke("subscribe", jv);
            BEAST_EXPECT(jv[jss::status] == "success");
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
        }

        {
            // Submit past ledger sequence transaction
            Json::Value payment;
            payment[jss::secret] = toBase58(generateSeed("alice"));
            payment[jss::tx_json] = pay("alice", "bob", XRP(1));
            payment[jss::tx_json][sfLastLedgerSequence.fieldName] = 1;
            auto jv = wsc->invoke("submit", payment);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(
                jv[jss::result][jss::engine_result] == "tefMAX_LEDGER");

            // Submit past sequence transaction
            payment[jss::tx_json] = pay("alice", "bob", XRP(1));
            payment[jss::tx_json][sfSequence.fieldName] = env.seq("alice") - 1;
            jv = wsc->invoke("submit", payment);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::engine_result] == "tefPAST_SEQ");

            // Submit future sequence transaction
            payment[jss::tx_json][sfSequence.fieldName] = env.seq("alice") + 1;
            jv = wsc->invoke("submit", payment);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::engine_result] == "terPRE_SEQ");

            // Submit transaction to bridge the sequence gap
            payment[jss::tx_json][sfSequence.fieldName] = env.seq("alice");
            jv = wsc->invoke("submit", payment);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::engine_result] == "tesSUCCESS");

            // Wait for the jobqueue to process everything
            env.app().getJobQueue().rendezvous();

            // Finalize transactions
            jv = wsc->invoke("ledger_accept");
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result].isMember(jss::ledger_current_index));
        }

        {
            // Check balances
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& ff = jv[jss::meta]["AffectedNodes"][1u]
                                   ["ModifiedNode"]["FinalFields"];
                return ff[jss::Account] == Account("bob").human() &&
                    ff["Balance"] == "10001000000";
            }));

            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                auto const& ff = jv[jss::meta]["AffectedNodes"][1u]
                                   ["ModifiedNode"]["FinalFields"];
                return ff[jss::Account] == Account("bob").human() &&
                    ff["Balance"] == "10002000000";
            }));
        }

        {
            // RPC unsubscribe to transactions stream
            Json::Value jv;
            jv[jss::streams] = Json::arrayValue;
            jv[jss::streams].append("transactions");
            jv = wsc->invoke("unsubscribe", jv);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }
    }

    /*
    Submit a normal payment. Client disconnects after the proposed
    transaction result is received.

    Client reconnects in the future. During this time it is presumed that the
    transaction should have succeeded.

    Upon reconnection, recent account transaction history is loaded.
    The submitted transaction should be detected, and the transaction should
    ultimately succeed.
    */
    void
    testReconnect()
    {
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");
        env.close();
        auto wsc = makeWSClient(env.app().config());

        {
            // Submit normal payment
            Json::Value jv;
            jv[jss::secret] = toBase58(generateSeed("alice"));
            jv[jss::tx_json] = pay("alice", "bob", XRP(1));
            jv = wsc->invoke("submit", jv);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::engine_result] == "tesSUCCESS");

            // Disconnect
            wsc.reset();

            // Server finalizes transaction
            env.close();
        }

        {
            // RPC account_tx
            Json::Value jv;
            jv[jss::account] = Account("bob").human();
            jv[jss::ledger_index_min] = -1;
            jv[jss::ledger_index_max] = -1;
            wsc = makeWSClient(env.app().config());
            jv = wsc->invoke("account_tx", jv);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }

            // Check balance
            auto ff = jv[jss::result][jss::transactions][0u][jss::meta]
                        ["AffectedNodes"][1u]["ModifiedNode"]["FinalFields"];
            BEAST_EXPECT(ff[jss::Account] == Account("bob").human());
            BEAST_EXPECT(ff["Balance"] == "10001000000");
        }
    }

    void
    testReconnectAfterWait()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice", "bob");
        env.close();
        auto wsc = makeWSClient(env.app().config());

        {
            // Submit normal payment
            Json::Value jv;
            jv[jss::secret] = toBase58(generateSeed("alice"));
            jv[jss::tx_json] = pay("alice", "bob", XRP(1));
            jv = wsc->invoke("submit", jv);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::engine_result] == "tesSUCCESS");

            // Finalize transaction
            jv = wsc->invoke("ledger_accept");
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result].isMember(jss::ledger_current_index));

            // Wait for the jobqueue to process everything
            env.app().getJobQueue().rendezvous();
        }

        {
            {
                // RPC subscribe to ledger stream
                Json::Value jv;
                jv[jss::streams] = Json::arrayValue;
                jv[jss::streams].append("ledger");
                jv = wsc->invoke("subscribe", jv);
                if (wsc->version() == 2)
                {
                    BEAST_EXPECT(
                        jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                    BEAST_EXPECT(
                        jv.isMember(jss::ripplerpc) &&
                        jv[jss::ripplerpc] == "2.0");
                    BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
                }
                BEAST_EXPECT(jv[jss::status] == "success");
            }

            // Close ledgers
            for (auto i = 0; i < 8; ++i)
            {
                auto jv = wsc->invoke("ledger_accept");
                if (wsc->version() == 2)
                {
                    BEAST_EXPECT(
                        jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                    BEAST_EXPECT(
                        jv.isMember(jss::ripplerpc) &&
                        jv[jss::ripplerpc] == "2.0");
                    BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
                }
                BEAST_EXPECT(
                    jv[jss::result].isMember(jss::ledger_current_index));

                // Wait for the jobqueue to process everything
                env.app().getJobQueue().rendezvous();

                BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jval) {
                    return jval[jss::type] == "ledgerClosed";
                }));
            }

            {
                // RPC unsubscribe to ledger stream
                Json::Value jv;
                jv[jss::streams] = Json::arrayValue;
                jv[jss::streams].append("ledger");
                jv = wsc->invoke("unsubscribe", jv);
                if (wsc->version() == 2)
                {
                    BEAST_EXPECT(
                        jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                    BEAST_EXPECT(
                        jv.isMember(jss::ripplerpc) &&
                        jv[jss::ripplerpc] == "2.0");
                    BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
                }
                BEAST_EXPECT(jv[jss::status] == "success");
            }
        }

        {
            // Disconnect, reconnect
            wsc = makeWSClient(env.app().config());
            {
                // RPC subscribe to ledger stream
                Json::Value jv;
                jv[jss::streams] = Json::arrayValue;
                jv[jss::streams].append("ledger");
                jv = wsc->invoke("subscribe", jv);
                if (wsc->version() == 2)
                {
                    BEAST_EXPECT(
                        jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                    BEAST_EXPECT(
                        jv.isMember(jss::ripplerpc) &&
                        jv[jss::ripplerpc] == "2.0");
                    BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
                }
                BEAST_EXPECT(jv[jss::status] == "success");
            }

            // Close ledgers
            for (auto i = 0; i < 2; ++i)
            {
                auto jv = wsc->invoke("ledger_accept");
                if (wsc->version() == 2)
                {
                    BEAST_EXPECT(
                        jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                    BEAST_EXPECT(
                        jv.isMember(jss::ripplerpc) &&
                        jv[jss::ripplerpc] == "2.0");
                    BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
                }
                BEAST_EXPECT(
                    jv[jss::result].isMember(jss::ledger_current_index));

                // Wait for the jobqueue to process everything
                env.app().getJobQueue().rendezvous();

                BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jval) {
                    return jval[jss::type] == "ledgerClosed";
                }));
            }

            {
                // RPC unsubscribe to ledger stream
                Json::Value jv;
                jv[jss::streams] = Json::arrayValue;
                jv[jss::streams].append("ledger");
                jv = wsc->invoke("unsubscribe", jv);
                if (wsc->version() == 2)
                {
                    BEAST_EXPECT(
                        jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                    BEAST_EXPECT(
                        jv.isMember(jss::ripplerpc) &&
                        jv[jss::ripplerpc] == "2.0");
                    BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
                }
                BEAST_EXPECT(jv[jss::status] == "success");
            }
        }

        {
            // RPC account_tx
            Json::Value jv;
            jv[jss::account] = Account("bob").human();
            jv[jss::ledger_index_min] = -1;
            jv[jss::ledger_index_max] = -1;
            wsc = makeWSClient(env.app().config());
            jv = wsc->invoke("account_tx", jv);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }

            // Check balance
            auto ff = jv[jss::result][jss::transactions][0u][jss::meta]
                        ["AffectedNodes"][1u]["ModifiedNode"]["FinalFields"];
            BEAST_EXPECT(ff[jss::Account] == Account("bob").human());
            BEAST_EXPECT(ff["Balance"] == "10001000000");
        }
    }

    void
    testAccountsProposed()
    {
        using namespace std::chrono_literals;
        using namespace jtx;
        Env env(*this);
        env.fund(XRP(10000), "alice");
        env.close();
        auto wsc = makeWSClient(env.app().config());

        {
            // RPC subscribe to accounts_proposed stream
            Json::Value jv;
            jv[jss::accounts_proposed] = Json::arrayValue;
            jv[jss::accounts_proposed].append(Account("alice").human());
            jv = wsc->invoke("subscribe", jv);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }

        {
            // Submit account_set transaction
            Json::Value jv;
            jv[jss::secret] = toBase58(generateSeed("alice"));
            jv[jss::tx_json] = fset("alice", 0);
            jv[jss::tx_json][jss::Fee] =
                static_cast<int>(env.current()->fees().base.drops());
            jv = wsc->invoke("submit", jv);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::result][jss::engine_result] == "tesSUCCESS");
        }

        {
            // Check stream update
            BEAST_EXPECT(wsc->findMsg(5s, [&](auto const& jv) {
                return jv[jss::transaction][jss::TransactionType] ==
                    jss::AccountSet;
            }));
        }

        {
            // RPC unsubscribe to accounts_proposed stream
            Json::Value jv;
            jv[jss::accounts_proposed] = Json::arrayValue;
            jv[jss::accounts_proposed].append(Account("alice").human());
            jv = wsc->invoke("unsubscribe", jv);
            if (wsc->version() == 2)
            {
                BEAST_EXPECT(
                    jv.isMember(jss::jsonrpc) && jv[jss::jsonrpc] == "2.0");
                BEAST_EXPECT(
                    jv.isMember(jss::ripplerpc) && jv[jss::ripplerpc] == "2.0");
                BEAST_EXPECT(jv.isMember(jss::id) && jv[jss::id] == 5);
            }
            BEAST_EXPECT(jv[jss::status] == "success");
        }
    }

    void
    run() override
    {
        testSequenceRealignment();
        testReconnect();
        testReconnectAfterWait();
        testAccountsProposed();
    }
};

BEAST_DEFINE_TESTSUITE(RobustTransaction, rpc, ripple);

}  // namespace test
}  // namespace ripple
