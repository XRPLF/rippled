
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {

class AccountTxPaging_test : public beast::unit_test::Suite
{
    static bool
    checkTransaction(json::Value const& tx, int sequence, int ledger)
    {
        return (
            tx[jss::tx][jss::Sequence].asInt() == sequence &&
            tx[jss::tx][jss::ledger_index].asInt() == ledger);
    }

    static auto
    next(
        test::jtx::Env& env,
        test::jtx::Account const& account,
        int ledgerMin,
        int ledgerMax,
        int limit,
        bool forward,
        json::Value const& marker = json::ValueType::Null)
    {
        json::Value jvc;
        jvc[jss::account] = account.human();
        jvc[jss::ledger_index_min] = ledgerMin;
        jvc[jss::ledger_index_max] = ledgerMax;
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
        Account const a1{"A1"};
        Account const a2{"A2"};
        Account const a3{"A3"};

        env.fund(XRP(10000), a1, a2, a3);
        env.close();

        env.trust(a3["USD"](1000), a1);
        env.trust(a2["USD"](1000), a1);
        env.trust(a3["USD"](1000), a2);
        env.close();

        for (auto i = 0; i < 5; ++i)
        {
            env(pay(a2, a1, a2["USD"](2)));
            env(pay(a3, a1, a3["USD"](2)));
            env(offer(a1, XRP(11), a1["USD"](1)));
            env(offer(a2, XRP(10), a2["USD"](1)));
            env(offer(a3, XRP(9), a3["USD"](1)));
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
            auto jrr = next(env, a3, 2, 5, 2, true);
            auto txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            BEAST_EXPECT(checkTransaction(txs[1u], 3, 3));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 2, 5, 2, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[1u], 4, 4));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 2, 5, 2, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 5));
            BEAST_EXPECT(checkTransaction(txs[1u], 5, 5));
            BEAST_EXPECT(!jrr[jss::marker]);
        }

        {
            // limit 1, 3 requests giving the first 3 txs
            auto jrr = next(env, a3, 3, 9, 1, true);
            auto txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 3, 9, 1, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 3, 9, 1, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            // continue with limit 3, to end of all txs
            jrr = next(env, a3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[1u], 4, 5));
            BEAST_EXPECT(checkTransaction(txs[2u], 5, 5));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 6, 6));
            BEAST_EXPECT(checkTransaction(txs[1u], 7, 6));
            BEAST_EXPECT(checkTransaction(txs[2u], 8, 7));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 9, 7));
            BEAST_EXPECT(checkTransaction(txs[1u], 10, 8));
            BEAST_EXPECT(checkTransaction(txs[2u], 11, 8));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 12, 9));
            BEAST_EXPECT(checkTransaction(txs[1u], 13, 9));
            BEAST_EXPECT(!jrr[jss::marker]);
        }

        {
            // limit 2, descending, 2 batches giving last 4 txs
            auto jrr = next(env, a3, 3, 9, 2, false);
            auto txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 13, 9));
            BEAST_EXPECT(checkTransaction(txs[1u], 12, 9));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 3, 9, 2, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 11, 8));
            BEAST_EXPECT(checkTransaction(txs[1u], 10, 8));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            // continue with limit 3 until all txs have been seen
            jrr = next(env, a3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 9, 7));
            BEAST_EXPECT(checkTransaction(txs[1u], 8, 7));
            BEAST_EXPECT(checkTransaction(txs[2u], 7, 6));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 6, 6));
            BEAST_EXPECT(checkTransaction(txs[1u], 5, 5));
            BEAST_EXPECT(checkTransaction(txs[2u], 4, 5));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[1u], 4, 4));
            BEAST_EXPECT(checkTransaction(txs[2u], 3, 3));
            if (!BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, a3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (!BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction(txs[0u], 3, 3));
            BEAST_EXPECT(!jrr[jss::marker]);
        }
    }

public:
    void
    run() override
    {
        testAccountTxPaging();
    }
};

BEAST_DEFINE_TESTSUITE(AccountTxPaging, app, xrpl);

}  // namespace xrpl
