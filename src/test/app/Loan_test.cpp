#include <test/app/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/multisign.h>
#include <test/jtx/noop.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>

#include <array>
#include <functional>
#include <utility>
#include <vector>

namespace xrpl::test {

class Loan_test : public LoanTestBase
{
private:
    void
    testLoanSet(FeatureBitset features)
    {
        using namespace jtx;

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        struct CaseArgs
        {
            bool requireAuth = false;
            bool authorizeBorrower = false;
            int initialXRP = 1'000'000;
        };

        auto const testCase = [&, this](
                                  std::function<void(Env&, BrokerInfo const&, MPTTester&)> mptTest,
                                  std::function<void(Env&, BrokerInfo const&)> iouTest,
                                  CaseArgs args = {}) {
            Env env(*this, features);
            env.fund(XRP(args.initialXRP), issuer, lender, borrower);
            env.close();
            if (args.requireAuth)
            {
                env(fset(issuer, asfRequireAuth));
                env.close();
            }

            // We need two different asset types, MPT and IOU. Prepare MPT
            // first
            MPTTester mptt{env, issuer, kMptInitNoFund};

            auto const kNone = LedgerSpecificFlags(0);
            mptt.create(
                {.flags = tfMPTCanTransfer | tfMPTCanLock |
                     (args.requireAuth ? tfMPTRequireAuth : kNone)});
            env.close();
            PrettyAsset const mptAsset = mptt.issuanceID();
            mptt.authorize({.account = lender});
            mptt.authorize({.account = borrower});
            env.close();
            if (args.requireAuth)
            {
                mptt.authorize({.account = issuer, .holder = lender});
                if (args.authorizeBorrower)
                    mptt.authorize({.account = issuer, .holder = borrower});
                env.close();
            }

            env(pay(issuer, lender, mptAsset(10'000'000)));
            env.close();

            // Prepare IOU
            PrettyAsset const iouAsset = issuer[iouCurrency_];
            env(trust(lender, iouAsset(10'000'000)));
            env(trust(borrower, iouAsset(10'000'000)));
            env.close();
            if (args.requireAuth)
            {
                env(trust(issuer, iouAsset(0), lender, tfSetfAuth));
                env(pay(issuer, lender, iouAsset(10'000'000)));
                if (args.authorizeBorrower)
                {
                    env(trust(issuer, iouAsset(0), borrower, tfSetfAuth));
                    env(pay(issuer, borrower, iouAsset(10'000)));
                }
            }
            else
            {
                env(pay(issuer, lender, iouAsset(10'000'000)));
                env(pay(issuer, borrower, iouAsset(10'000)));
            }
            env.close();

            // Create vaults and loan brokers
            std::array const assets{mptAsset, iouAsset};
            std::vector<BrokerInfo> brokers;
            brokers.reserve(assets.size());
            for (auto const& asset : assets)
            {
                brokers.emplace_back(createVaultAndBroker(env, asset, lender));
            }

            if (mptTest)
                mptTest(env, brokers[0], mptt);
            if (iouTest)
                iouTest(env, brokers[1]);
        };

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT issuer is borrower, issuer submits");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));

                testcase("MPT issuer is borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(issuer),
                    Sig(sfCounterpartySignature, issuer),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU issuer is borrower, issuer submits");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));

                testcase("IOU issuer is borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(issuer),
                    Sig(sfCounterpartySignature, issuer),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT unauthorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});

                testcase("MPT unauthorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU unauthorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});

                testcase("IOU unauthorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
            },
            CaseArgs{.requireAuth = true});

        auto const [acctReserve, incReserve] = [this]() -> std::pair<int, int> {
            Env const env{*this, testableAmendments()};
            return {
                env.current()->fees().accountReserve(0, 1).drops() / kDropsPerXrp.drops(),
                env.current()->fees().increment.drops() / kDropsPerXrp.drops()};
        }();

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, borrower has "
                    "no reserve");
                mptt.authorize({.account = borrower, .flags = tfMPTUnauthorize});
                env.close();

                auto const mptoken = keylet::mptoken(mptt.issuanceID(), borrower);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 == nullptr);

                // Burn some XRP
                env(noop(borrower), Fee(XRP((acctReserve * 2) + (incReserve * 2))));
                env.close();

                // Cannot create loan, not enough reserve to create MPToken
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecINSUFFICIENT_RESERVE});
                env.close();

                // Can create loan now, will implicitly create MPToken
                env(pay(issuer, borrower, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 != nullptr);
            },
            {},
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

        testCase(
            {},
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, borrower has "
                    "no reserve");
                // Remove trust line from borrower to issuer
                env.trust(broker.asset(0), borrower);
                env.close();

                env(pay(borrower, issuer, broker.asset(10'000)));
                env.close();
                auto const trustline = keylet::trustLine(borrower, broker.asset.raw().get<Issue>());
                auto const sleLine1 = env.le(trustline);
                BEAST_EXPECT(sleLine1 == nullptr);

                // Burn some XRP
                env(noop(borrower), Fee(XRP((acctReserve * 2) + (incReserve * 2))));
                env.close();

                // Cannot create loan, not enough reserve to create trust line
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                // Can create loan now, will implicitly create trust line
                env(pay(issuer, borrower, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleLine2 = env.le(trustline);
                BEAST_EXPECT(sleLine2 != nullptr);
            },
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, lender has "
                    "no reserve");
                auto const mptoken = keylet::mptoken(mptt.issuanceID(), lender);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 != nullptr);

                env(pay(lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                env.close();

                mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 == nullptr);

                // Burn some XRP
                env(noop(lender), Fee(XRP(incReserve)));
                env.close();

                // Cannot create loan, not enough reserve to create MPToken
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecINSUFFICIENT_RESERVE});
                env.close();

                // Can create loan now, will implicitly create MPToken
                env(pay(issuer, lender, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleMPT3 = env.le(mptoken);
                BEAST_EXPECT(sleMPT3 != nullptr);
            },
            {},
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

        testCase(
            {},
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, lender has no "
                    "reserve");
                // Remove trust line from lender to issuer
                env.trust(broker.asset(0), lender);
                env.close();

                auto const trustline = keylet::trustLine(lender, broker.asset.raw().get<Issue>());
                auto const sleLine1 = env.le(trustline);
                BEAST_EXPECT(sleLine1 != nullptr);

                env(pay(lender, issuer, broker.asset(abs(sleLine1->at(sfBalance).value()))));
                env.close();
                auto const sleLine2 = env.le(trustline);
                BEAST_EXPECT(sleLine2 == nullptr);

                // Burn some XRP
                env(noop(lender), Fee(XRP(incReserve)));
                env.close();

                // Cannot create loan, not enough reserve to create trust line
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_LINE_INSUF_RESERVE});
                env.close();

                // Can create loan now, will implicitly create trust line
                env(pay(issuer, lender, XRP(incReserve)));
                env.close();
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
                env.close();

                auto const sleLine3 = env.le(trustline);
                BEAST_EXPECT(sleLine3 != nullptr);
            },
            CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, unauthorized lender");
                auto const mptoken = keylet::mptoken(mptt.issuanceID(), lender);
                auto const sleMPT1 = env.le(mptoken);
                BEAST_EXPECT(sleMPT1 != nullptr);

                env(pay(lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                env.close();

                mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                env.close();

                auto const sleMPT2 = env.le(mptoken);
                BEAST_EXPECT(sleMPT2 == nullptr);

                // Cannot create loan, lender not authorized to receive fee
                env(set(borrower, broker.brokerID, principalRequest),
                    kLoanOriginationFee(broker.asset(1).value()),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
                env.close();

                // Cannot create loan, even without an origination fee
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter{tecNO_AUTH});
                env.close();

                // No MPToken for lender - no authorization and no payment
                auto const sleMPT3 = env.le(mptoken);
                BEAST_EXPECT(sleMPT3 == nullptr);
            },
            {},
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU authorized borrower, borrower submits");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("MPT authorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();

                testcase("IOU authorized borrower, lender submits");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Sig(sfCounterpartySignature, borrower),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        jtx::Account const alice{"alice"};
        jtx::Account const bella{"bella"};
        auto const msigSetup = [&](Env& env, Account const& account) {
            json::Value const tx1 = signers(account, 2, {{alice, 1}, {bella, 1}});
            env(tx1);
            env.close();
        };

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                msigSetup(env, lender);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, borrower submits, lender "
                    "multisign");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                msigSetup(env, lender);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, borrower submits, lender "
                    "multisign");
                env(set(borrower, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                msigSetup(env, borrower);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "MPT authorized borrower, lender submits, borrower "
                    "multisign");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            [&, this](Env& env, BrokerInfo const& broker) {
                using namespace loan;
                msigSetup(env, borrower);
                Number const principalRequest = broker.asset(1'000).value();

                testcase(
                    "IOU authorized borrower, lender submits, borrower "
                    "multisign");
                env(set(lender, broker.brokerID, principalRequest),
                    kCounterparty(borrower),
                    Msig(sfCounterpartySignature, alice, bella),
                    Fee(env.current()->fees().base * 5));
            },
            CaseArgs{.requireAuth = true, .authorizeBorrower = true});

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();
                Vault const vault{env};
                auto tx = vault.set({.owner = lender, .id = broker.vaultID});
                tx[sfAssetsMaximum] = BrokerParameters::defaults().vaultDeposit;
                env(tx);
                env.close();

                testcase("Vault at maximum value");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    kInterestRate(TenthBips32(10'000)),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    Ter(tecLIMIT_EXCEEDED));
            },
            nullptr);

        testCase(
            [&, this](Env& env, BrokerInfo const& broker, auto&) {
                using namespace loan;
                Number const principalRequest = broker.asset(1'000).value();
                Vault const vault{env};
                auto tx = vault.set({.owner = lender, .id = broker.vaultID});
                tx[sfAssetsMaximum] =
                    BrokerParameters::defaults().vaultDeposit + broker.asset(1).number();
                env(tx);
                env.close();

                testcase("Vault maximum value exceeded");
                env(set(issuer, broker.brokerID, principalRequest),
                    kCounterparty(lender),
                    kInterestRate(TenthBips32(100'000)),
                    Sig(sfCounterpartySignature, lender),
                    Fee(env.current()->fees().base * 5),
                    kPaymentTotal(2),
                    kPaymentInterval(3600 * 24),
                    Ter(tecLIMIT_EXCEEDED));
            },
            nullptr);
    }

public:
    void
    run() override
    {
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2}, all_))
            testLoanSet(features);
    }
};

BEAST_DEFINE_TESTSUITE(Loan, tx, xrpl);

}  // namespace xrpl::test
