//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2019 Ripple Labs Inc.

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

#include <ripple/app/paths/AMMContext.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/app/paths/impl/StrandFlow.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/random.h>
#include <ripple/core/Config.h>
#include <ripple/ledger/ApplyViewImpl.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/PathSet.h>

namespace ripple {
namespace test {

struct RippleCalcTestParams
{
    AccountID srcAccount;
    AccountID dstAccount;

    STAmount dstAmt;
    std::optional<STAmount> sendMax;

    STPathSet paths;

    explicit RippleCalcTestParams(Json::Value const& jv)
        : srcAccount{*parseBase58<AccountID>(jv[jss::Account].asString())}
        , dstAccount{*parseBase58<AccountID>(jv[jss::Destination].asString())}
        , dstAmt{amountFromJson(sfAmount, jv[jss::Amount])}
    {
        if (jv.isMember(jss::SendMax))
            sendMax = amountFromJson(sfSendMax, jv[jss::SendMax]);

        if (jv.isMember(jss::Paths))
        {
            // paths is an array of arrays
            // each leaf element will be of the form
            for (auto const& path : jv[jss::Paths])
            {
                STPath p;
                for (auto const& pe : path)
                {
                    if (pe.isMember(jss::account))
                    {
                        assert(
                            !pe.isMember(jss::currency) &&
                            !pe.isMember(jss::issuer));
                        p.emplace_back(
                            *parseBase58<AccountID>(
                                pe[jss::account].asString()),
                            std::nullopt,
                            std::nullopt);
                    }
                    else if (
                        pe.isMember(jss::currency) && pe.isMember(jss::issuer))
                    {
                        auto const currency =
                            to_currency(pe[jss::currency].asString());
                        std::optional<AccountID> issuer;
                        if (!isXRP(currency))
                            issuer = *parseBase58<AccountID>(
                                pe[jss::issuer].asString());
                        else
                            assert(isXRP(*parseBase58<AccountID>(
                                pe[jss::issuer].asString())));
                        p.emplace_back(std::nullopt, currency, issuer);
                    }
                    else
                    {
                        assert(0);
                    }
                }
                paths.emplace_back(std::move(p));
            }
        }
    }
};

// Class to randomly set an account's transfer rate, quality in, quality out,
// and initial balance
class RandomAccountParams
{
    beast::xor_shift_engine engine_;
    std::uint32_t const trustAmount_;
    // Balance to set if an account redeems into another account. Otherwise
    // the balance will be zero. Since we are testing quality measures, the
    // payment should not use multiple qualities, so the initialBalance
    // needs to be able to handle an entire payment (otherwise an account
    // will go from redeeming to issuing and the fees/qualities can change)
    std::uint32_t const initialBalance_;

    // probability of changing a value from its default
    constexpr static double probChangeDefault_ = 0.75;
    // probability that an account redeems into another account
    constexpr static double probRedeem_ = 0.5;
    std::uniform_real_distribution<> zeroOneDist_{0.0, 1.0};
    std::uniform_real_distribution<> transferRateDist_{1.0, 2.0};
    std::uniform_real_distribution<> qualityPercentDist_{80, 120};

    bool
    shouldSet()
    {
        return zeroOneDist_(engine_) <= probChangeDefault_;
    };

    void
    maybeInsertQuality(Json::Value& jv, QualityDirection qDir)
    {
        if (!shouldSet())
            return;

        auto const percent = qualityPercentDist_(engine_);
        auto const& field =
            qDir == QualityDirection::in ? sfQualityIn : sfQualityOut;
        auto const value =
            static_cast<std::uint32_t>((percent / 100) * QUALITY_ONE);
        jv[field.jsonName] = value;
    };

    // Setup the trust amounts and in/out qualities (but not the balances)
    void
    setupTrustLine(
        jtx::Env& env,
        jtx::Account const& acc,
        jtx::Account const& peer,
        Currency const& currency)
    {
        using namespace jtx;
        IOU const iou{peer, currency};
        Json::Value jv = trust(acc, iou(trustAmount_));
        maybeInsertQuality(jv, QualityDirection::in);
        maybeInsertQuality(jv, QualityDirection::out);
        env(jv);
        env.close();
    };

public:
    explicit RandomAccountParams(
        std::uint32_t trustAmount = 100,
        std::uint32_t initialBalance = 50)
        // Use a deterministic seed so the unit tests run in a reproducible way
        : engine_{1977u}
        , trustAmount_{trustAmount}
        , initialBalance_{initialBalance} {};

    void
    maybeSetTransferRate(jtx::Env& env, jtx::Account const& acc)
    {
        if (shouldSet())
            env(rate(acc, transferRateDist_(engine_)));
    }

    // Set the initial balance, taking into account the qualities
    void
    setInitialBalance(
        jtx::Env& env,
        jtx::Account const& acc,
        jtx::Account const& peer,
        Currency const& currency)
    {
        using namespace jtx;
        IOU const iou{acc, currency};
        // This payment sets the acc's balance to `initialBalance`.
        // Since input qualities complicate this payment, use `sendMax` with
        // `initialBalance` to make sure the balance is set correctly.
        env(pay(peer, acc, iou(trustAmount_)),
            sendmax(iou(initialBalance_)),
            txflags(tfPartialPayment));
        env.close();
    }

    void
    maybeSetInitialBalance(
        jtx::Env& env,
        jtx::Account const& acc,
        jtx::Account const& peer,
        Currency const& currency)
    {
        using namespace jtx;
        if (zeroOneDist_(engine_) > probRedeem_)
            return;
        setInitialBalance(env, acc, peer, currency);
    }

    // Setup the trust amounts and in/out qualities (but not the balances) on
    // both sides of the trust line
    void
    setupTrustLines(
        jtx::Env& env,
        jtx::Account const& acc1,
        jtx::Account const& acc2,
        Currency const& currency)
    {
        setupTrustLine(env, acc1, acc2, currency);
        setupTrustLine(env, acc2, acc1, currency);
    };
};

class TheoreticalQuality_test : public beast::unit_test::suite
{
    static std::string
    prettyQuality(Quality const& q)
    {
        std::stringstream sstr;
        STAmount rate = q.rate();
        sstr << rate << " (" << q << ")";
        return sstr.str();
    };

    template <class Stream>
    static void
    logStrand(Stream& stream, Strand const& strand)
    {
        stream << "Strand:\n";
        for (auto const& step : strand)
            stream << "\n" << *step;
        stream << "\n\n";
    };

    void
    testCase(
        RippleCalcTestParams const& rcp,
        std::shared_ptr<ReadView const> closed,
        std::optional<Quality> const& expectedQ = {})
    {
        PaymentSandbox sb(closed.get(), tapNONE);
        AMMContext ammContext(rcp.srcAccount, false);

        auto const sendMaxIssue = [&rcp]() -> std::optional<Issue> {
            if (rcp.sendMax)
                return rcp.sendMax->issue();
            return std::nullopt;
        }();

        beast::Journal dummyJ{beast::Journal::getNullSink()};

        auto sr = toStrands(
            sb,
            rcp.srcAccount,
            rcp.dstAccount,
            rcp.dstAmt.issue(),
            /*limitQuality*/ std::nullopt,
            sendMaxIssue,
            rcp.paths,
            /*defaultPaths*/ rcp.paths.empty(),
            sb.rules().enabled(featureOwnerPaysFee),
            OfferCrossing::no,
            ammContext,
            dummyJ);

        BEAST_EXPECT(sr.first == tesSUCCESS);

        if (sr.first != tesSUCCESS)
            return;

        // Due to the floating point calculations, theoretical and actual
        // qualities are not expected to always be exactly equal. However, they
        // should always be very close. This function checks that that two
        // qualities are "close enough".
        auto compareClose = [](Quality const& q1, Quality const& q2) {
            // relative diff is fabs(a-b)/min(a,b)
            // can't get access to internal value. Use the rate
            constexpr double tolerance = 0.0000001;
            return relativeDistance(q1, q2) <= tolerance;
        };

        for (auto const& strand : sr.second)
        {
            Quality const theoreticalQ = *qualityUpperBound(sb, strand);
            auto const f = flow<IOUAmount, IOUAmount>(
                sb, strand, IOUAmount(10, 0), IOUAmount(5, 0), dummyJ);
            BEAST_EXPECT(f.success);
            Quality const actualQ(f.out, f.in);
            if (actualQ != theoreticalQ && !compareClose(actualQ, theoreticalQ))
            {
                BEAST_EXPECT(actualQ == theoreticalQ);  // get the failure
                log << "\nAcutal != Theoretical\n";
                log << "\nTQ: " << prettyQuality(theoreticalQ) << "\n";
                log << "AQ: " << prettyQuality(actualQ) << "\n";
                logStrand(log, strand);
            }
            if (expectedQ && expectedQ != theoreticalQ &&
                !compareClose(*expectedQ, theoreticalQ))
            {
                BEAST_EXPECT(expectedQ == theoreticalQ);  // get the failure
                log << "\nExpected != Theoretical\n";
                log << "\nTQ: " << prettyQuality(theoreticalQ) << "\n";
                log << "EQ: " << prettyQuality(*expectedQ) << "\n";
                logStrand(log, strand);
            }
        };
    }

public:
    void
    testDirectStep(std::optional<int> const& reqNumIterations)
    {
        testcase("Direct Step");

        // clang-format off

        // Set up a payment through four accounts: alice -> bob -> carol -> dan
        // For each relevant trust line on the path, there are three things that can vary:
        //  1) input quality
        //  2) output quality
        //  3) debt direction
        // For each account, there is one thing that can vary:
        //  1) transfer rate

        // clang-format on

        using namespace jtx;

        auto const currency = to_currency("USD");

        constexpr std::size_t const numAccounts = 4;

        // There are three relevant trust lines: `alice->bob`, `bob->carol`, and
        // `carol->dan`. There are four accounts. If we count the number of
        // combinations of parameters where a parameter is changed from its
        // default value, there are
        // 2^(num_trust_lines*num_trust_qualities+numAccounts) combinations of
        // values to test, or 2^13 combinations. Use this value to set the
        // number of iterations. Note however that many of these parameter
        // combinations run essentially the same test. For example, changing the
        // quality values for bob and carol test almost the same thing.
        // Similarly, changing the transfer rates on bob and carol test almost
        // the same thing. Instead of systematically running these 8k tests,
        // randomly sample the test space.
        int const numTestIterations = reqNumIterations.value_or(250);

        constexpr std::uint32_t paymentAmount = 1;

        // Class to randomly set account transfer rates, qualities, and other
        // params.
        RandomAccountParams rndAccParams;

        // Tests are sped up by a factor of 2 if a new environment isn't created
        // on every iteration.
        Env env(*this, supported_amendments());
        for (int i = 0; i < numTestIterations; ++i)
        {
            auto const iterAsStr = std::to_string(i);
            // New set of accounts on every iteration so the environment doesn't
            // need to be recreated (2x speedup)
            auto const alice = Account("alice" + iterAsStr);
            auto const bob = Account("bob" + iterAsStr);
            auto const carol = Account("carol" + iterAsStr);
            auto const dan = Account("dan" + iterAsStr);
            std::array<Account, numAccounts> accounts{{alice, bob, carol, dan}};
            static_assert(
                numAccounts == 4, "Path is only correct for four accounts");
            path const accountsPath(accounts[1], accounts[2]);
            env.fund(XRP(10000), alice, bob, carol, dan);
            env.close();

            // iterate through all pairs of accounts, randomly set the transfer
            // rate, qIn, qOut, and if the account issues or redeems
            for (std::size_t ii = 0; ii < numAccounts; ++ii)
            {
                rndAccParams.maybeSetTransferRate(env, accounts[ii]);
                // The payment is from:
                // account[0] -> account[1] -> account[2] -> account[3]
                // set the trust lines and initial balances for each pair of
                // neighboring accounts
                std::size_t const j = ii + 1;
                if (j == numAccounts)
                    continue;

                rndAccParams.setupTrustLines(
                    env, accounts[ii], accounts[j], currency);
                rndAccParams.maybeSetInitialBalance(
                    env, accounts[ii], accounts[j], currency);
            }

            // Accounts are set up, make the payment
            IOU const iou{accounts.back(), currency};
            RippleCalcTestParams rcp{env.json(
                pay(accounts.front(), accounts.back(), iou(paymentAmount)),
                accountsPath,
                txflags(tfNoRippleDirect))};

            testCase(rcp, env.closed());
        }
    }

    void
    testBookStep(std::optional<int> const& reqNumIterations)
    {
        testcase("Book Step");
        using namespace jtx;

        // clang-format off

        // Setup a payment through an offer: alice (USD/bob) -> bob -> (USD/bob)|(EUR/carol) -> carol -> dan
        // For each relevant trust line, vary input quality, output quality, debt direction.
        // For each account, vary transfer rate.
        // The USD/bob|EUR/carol offer owner is "Oscar"

        // clang-format on

        int const numTestIterations = reqNumIterations.value_or(100);

        constexpr std::uint32_t paymentAmount = 1;

        Currency const eurCurrency = to_currency("EUR");
        Currency const usdCurrency = to_currency("USD");

        // Class to randomly set account transfer rates, qualities, and other
        // params.
        RandomAccountParams rndAccParams;

        // Speed up tests by creating the environment outside the loop
        // (factor of 2 speedup on the DirectStep tests)
        Env env(*this, supported_amendments());
        for (int i = 0; i < numTestIterations; ++i)
        {
            auto const iterAsStr = std::to_string(i);
            auto const alice = Account("alice" + iterAsStr);
            auto const bob = Account("bob" + iterAsStr);
            auto const carol = Account("carol" + iterAsStr);
            auto const dan = Account("dan" + iterAsStr);
            auto const oscar = Account("oscar" + iterAsStr);  // offer owner
            auto const USDB = bob["USD"];
            auto const EURC = carol["EUR"];
            constexpr std::size_t const numAccounts = 5;
            std::array<Account, numAccounts> accounts{
                {alice, bob, carol, dan, oscar}};

            // sendmax should be in USDB and delivered amount should be in EURC
            // normalized path should be:
            // alice -> bob -> (USD/bob)|(EUR/carol) -> carol -> dan
            path const bookPath(~EURC);

            env.fund(XRP(10000), alice, bob, carol, dan, oscar);
            env.close();

            for (auto const& acc : accounts)
                rndAccParams.maybeSetTransferRate(env, acc);

            for (auto const& currency : {usdCurrency, eurCurrency})
            {
                rndAccParams.setupTrustLines(
                    env, alice, bob, currency);  // first step in payment
                rndAccParams.setupTrustLines(
                    env, carol, dan, currency);  // last step in payment
                rndAccParams.setupTrustLines(
                    env, oscar, bob, currency);  // offer owner
                rndAccParams.setupTrustLines(
                    env, oscar, carol, currency);  // offer owner
            }

            rndAccParams.maybeSetInitialBalance(env, alice, bob, usdCurrency);
            rndAccParams.maybeSetInitialBalance(env, carol, dan, eurCurrency);
            rndAccParams.setInitialBalance(env, oscar, bob, usdCurrency);
            rndAccParams.setInitialBalance(env, oscar, carol, eurCurrency);

            env(offer(oscar, USDB(50), EURC(50)));
            env.close();

            // Accounts are set up, make the payment
            IOU const srcIOU{bob, usdCurrency};
            IOU const dstIOU{carol, eurCurrency};
            RippleCalcTestParams rcp{env.json(
                pay(alice, dan, dstIOU(paymentAmount)),
                sendmax(srcIOU(100 * paymentAmount)),
                bookPath,
                txflags(tfNoRippleDirect))};

            testCase(rcp, env.closed());
        }
    }

    void
    testRelativeQDistance()
    {
        testcase("Relative quality distance");

        auto toQuality = [](std::uint64_t mantissa,
                            int exponent = 0) -> Quality {
            // The only way to construct a Quality from an STAmount is to take
            // their ratio. Set the denominator STAmount to `one` to easily
            // create a quality from a single amount
            STAmount const one{noIssue(), 1};
            STAmount const v{noIssue(), mantissa, exponent};
            return Quality{one, v};
        };

        BEAST_EXPECT(relativeDistance(toQuality(100), toQuality(100)) == 0);
        BEAST_EXPECT(relativeDistance(toQuality(100), toQuality(100, 1)) == 9);
        BEAST_EXPECT(relativeDistance(toQuality(100), toQuality(110)) == .1);
        BEAST_EXPECT(
            relativeDistance(toQuality(100, 90), toQuality(110, 90)) == .1);
        BEAST_EXPECT(
            relativeDistance(toQuality(100, 90), toQuality(110, 91)) == 10);
        BEAST_EXPECT(
            relativeDistance(toQuality(100, 0), toQuality(100, 90)) == 1e90);
        // Make the mantissa in the smaller value bigger than the mantissa in
        // the larger value. Instead of checking the exact result, we check that
        // it's large. If the values did not compare correctly in
        // `relativeDistance`, then the returned value would be negative.
        BEAST_EXPECT(
            relativeDistance(toQuality(102, 0), toQuality(101, 90)) >= 1e89);
    }

    void
    run() override
    {
        // Use the command line argument `--unittest-arg=500 ` to change the
        // number of iterations to 500
        auto const numIterations = [s = arg()]() -> std::optional<int> {
            if (s.empty())
                return std::nullopt;
            try
            {
                std::size_t pos;
                auto const r = stoi(s, &pos);
                if (pos != s.size())
                    return std::nullopt;
                return r;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }();
        testRelativeQDistance();
        testDirectStep(numIterations);
        testBookStep(numIterations);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(TheoreticalQuality, app, ripple, 3);

}  // namespace test
}  // namespace ripple
