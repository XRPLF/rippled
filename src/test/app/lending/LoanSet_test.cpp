#include <test/app/lending/LoanTestBase.h>
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
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace xrpl::test {

class LoanSet_test : public LoanTestBase
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
            LoanFlow flow = LoanFlow::OneStep;
        };

        auto const testCase = [&, this](
                                  std::function<void(Env&, BrokerInfo const&, MPTTester&)> mptTest,
                                  std::function<void(Env&, BrokerInfo const&)> iouTest,
                                  CaseArgs args = {}) {
            if (args.flow == LoanFlow::TwoStep && !features[featureLendingProtocolV1_1])
                return;

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

        // Submit a LoanSet under the requested flow.
        //
        // One-step: `submitter` signs the outer tx and `counterparty` is
        // named in the Counterparty field and supplies the
        // CounterpartySignature.
        //
        // Two-step: the LoanBroker owner (`lender`) always submits the
        // proposal, naming as the borrower whichever of `submitter` or
        // `counterparty` is not `lender`. There is no
        // CounterpartySignature and no LoanAccept -- callers that need
        // the loan to end up active must submit the LoanAccept
        // themselves.
        auto const submitSet = [&](Env& env,
                                   LoanFlow flow,
                                   BrokerInfo const& broker,
                                   Account const& submitter,
                                   Account const& counterparty,
                                   Number const& principalRequest,
                                   auto const&... extras) -> uint256 {
            using namespace loan;
            using namespace std::chrono_literals;

            // The keylet the LoanSet will (or would) create, so the caller
            // can drive a follow-up LoanAccept in the two-step flow.
            auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
            auto const loanKey =
                keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)))
                    .key;

            if (flow == LoanFlow::OneStep)
            {
                env(set(submitter, broker.brokerID, principalRequest),
                    kCounterparty(counterparty),
                    Sig(sfCounterpartySignature, counterparty),
                    Fee(env.current()->fees().base * 5),
                    extras...);
            }
            else
            {
                Account const& theBorrower =
                    submitter.id() == lender.id() ? counterparty : submitter;
                std::uint32_t const startDate = (env.now() + 1h).time_since_epoch().count();
                env(set(lender, broker.brokerID, principalRequest),
                    kBorrower(theBorrower),
                    kStartDate(startDate),
                    Fee(env.current()->fees().base * 5),
                    extras...);
            }
            return loanKey;
        };

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                [&, flow, this](Env& env, BrokerInfo const& broker, auto&) {
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "MPT issuer is borrower, issuer submits (" << flowLabel << ")";
                    submitSet(env, flow, broker, issuer, lender, principalRequest);

                    // Only the broker owner may submit in the two-step
                    // flow, so the "lender submits" variant is one-step
                    // only.
                    if (flow == LoanFlow::OneStep)
                    {
                        testcase("MPT issuer is borrower, lender submits");
                        submitSet(env, flow, broker, lender, issuer, principalRequest);
                    }
                },
                [&, flow, this](Env& env, BrokerInfo const& broker) {
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "IOU issuer is borrower, issuer submits (" << flowLabel << ")";
                    submitSet(env, flow, broker, issuer, lender, principalRequest);

                    if (flow == LoanFlow::OneStep)
                    {
                        testcase("IOU issuer is borrower, lender submits");
                        submitSet(env, flow, broker, lender, issuer, principalRequest);
                    }
                },
                CaseArgs{.requireAuth = true, .flow = flow});
        }

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                [&, flow](Env& env, BrokerInfo const& broker, auto&) {
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "MPT unauthorized borrower, borrower submits (" << flowLabel << ")";
                    submitSet(
                        env, flow, broker, borrower, lender, principalRequest, Ter{tecNO_AUTH});

                    if (flow == LoanFlow::OneStep)
                    {
                        testcase("MPT unauthorized borrower, lender submits");
                        submitSet(
                            env, flow, broker, lender, borrower, principalRequest, Ter{tecNO_AUTH});
                    }
                },
                [&, flow](Env& env, BrokerInfo const& broker) {
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "IOU unauthorized borrower, borrower submits (" << flowLabel << ")";
                    submitSet(
                        env, flow, broker, borrower, lender, principalRequest, Ter{tecNO_AUTH});

                    if (flow == LoanFlow::OneStep)
                    {
                        testcase("IOU unauthorized borrower, lender submits");
                        submitSet(
                            env, flow, broker, lender, borrower, principalRequest, Ter{tecNO_AUTH});
                    }
                },
                CaseArgs{.requireAuth = true, .flow = flow});
        }

        auto const [acctReserve, incReserve] = [this]() -> std::pair<int, int> {
            Env const env{*this, testableAmendments()};
            return {
                env.current()->fees().accountReserve(0, 1).drops() / kDropsPerXrp.drops(),
                env.current()->fees().increment.drops() / kDropsPerXrp.drops()};
        }();

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                [&, flow](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                    using namespace loan;
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "MPT authorized borrower, borrower has no "
                                "reserve ("
                             << flowLabel << ")";
                    mptt.authorize({.account = borrower, .flags = tfMPTUnauthorize});
                    env.close();

                    auto const mptoken = keylet::mptoken(mptt.issuanceID(), borrower);
                    BEAST_EXPECT(env.le(mptoken) == nullptr);

                    // Burn some XRP
                    env(noop(borrower), Fee(XRP((acctReserve * 2) + (incReserve * 2))));
                    env.close();

                    if (flow == LoanFlow::OneStep)
                    {
                        // Cannot create loan: borrower cannot afford MPToken
                        // reserve on disbursement.
                        submitSet(
                            env,
                            flow,
                            broker,
                            borrower,
                            lender,
                            principalRequest,
                            Ter{tecINSUFFICIENT_RESERVE});
                        env.close();

                        env(pay(issuer, borrower, XRP(incReserve)));
                        env.close();
                        submitSet(env, flow, broker, borrower, lender, principalRequest);
                        env.close();
                    }
                    else
                    {
                        // Two-step: the LoanBroker owner (lender) is charged
                        // the reserve for the pending loan. Top up the lender
                        // so they have room for the additional owner slot.
                        env(pay(issuer, lender, XRP(incReserve)));
                        env.close();

                        // LoanSet succeeds (the broker owner carries the
                        // reserve for the pending loan); the borrower's
                        // MPToken reserve check only fires on LoanAccept.
                        auto const loanKey =
                            submitSet(env, flow, broker, borrower, lender, principalRequest);
                        env.close();

                        env(accept(borrower, loanKey), Ter{tecINSUFFICIENT_RESERVE});
                        env.close();

                        env(pay(issuer, borrower, XRP(incReserve)));
                        env.close();
                        env(accept(borrower, loanKey));
                        env.close();
                    }

                    BEAST_EXPECT(env.le(mptoken) != nullptr);
                },
                {},
                CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1, .flow = flow});
        }

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                {},
                [&, flow](Env& env, BrokerInfo const& broker) {
                    using namespace loan;
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "IOU authorized borrower, borrower has no "
                                "reserve ("
                             << flowLabel << ")";
                    // Remove trust line from borrower to issuer
                    env.trust(broker.asset(0), borrower);
                    env.close();

                    env(pay(borrower, issuer, broker.asset(10'000)));
                    env.close();
                    auto const trustline =
                        keylet::trustLine(borrower, broker.asset.raw().get<Issue>());
                    BEAST_EXPECT(env.le(trustline) == nullptr);

                    // Burn some XRP
                    env(noop(borrower), Fee(XRP((acctReserve * 2) + (incReserve * 2))));
                    env.close();

                    if (flow == LoanFlow::OneStep)
                    {
                        // Cannot create loan: borrower cannot afford trust
                        // line reserve on disbursement.
                        submitSet(
                            env,
                            flow,
                            broker,
                            borrower,
                            lender,
                            principalRequest,
                            Ter{tecNO_LINE_INSUF_RESERVE});
                        env.close();

                        env(pay(issuer, borrower, XRP(incReserve)));
                        env.close();
                        submitSet(env, flow, broker, borrower, lender, principalRequest);
                        env.close();
                    }
                    else
                    {
                        // Two-step: the LoanBroker owner (lender) is charged
                        // the reserve for the pending loan. Top up the lender
                        // so they have room for the additional owner slot.
                        env(pay(issuer, lender, XRP(incReserve)));
                        env.close();

                        // LoanSet succeeds; the borrower's trust line reserve
                        // check only fires on LoanAccept.
                        auto const loanKey =
                            submitSet(env, flow, broker, borrower, lender, principalRequest);
                        env.close();

                        env(accept(borrower, loanKey), Ter{tecNO_LINE_INSUF_RESERVE});
                        env.close();

                        env(pay(issuer, borrower, XRP(incReserve)));
                        env.close();
                        env(accept(borrower, loanKey));
                        env.close();
                    }

                    BEAST_EXPECT(env.le(trustline) != nullptr);
                },
                CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1, .flow = flow});
        }

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                [&, flow](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                    using namespace loan;
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "MPT authorized borrower, lender has no "
                                "reserve ("
                             << flowLabel << ")";
                    auto const mptoken = keylet::mptoken(mptt.issuanceID(), lender);
                    auto const sleMPT1 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT1 != nullptr);

                    env(pay(lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                    env.close();

                    mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                    env.close();

                    BEAST_EXPECT(env.le(mptoken) == nullptr);

                    // Burn some XRP
                    env(noop(lender), Fee(XRP(incReserve)));
                    env.close();

                    // Both flows need one extra owner-count increment on the
                    // lender: the disburse-time MPToken in one-step, the
                    // pending-loan reserve in two-step. Both return the
                    // generic tecINSUFFICIENT_RESERVE.
                    submitSet(
                        env,
                        flow,
                        broker,
                        borrower,
                        lender,
                        principalRequest,
                        kLoanOriginationFee(broker.asset(1).value()),
                        Ter{tecINSUFFICIENT_RESERVE});
                    env.close();

                    // Top up the lender and retry.
                    env(pay(issuer, lender, XRP(incReserve)));
                    env.close();
                    auto const loanKey = submitSet(
                        env,
                        flow,
                        broker,
                        borrower,
                        lender,
                        principalRequest,
                        kLoanOriginationFee(broker.asset(1).value()));
                    env.close();

                    if (flow == LoanFlow::TwoStep)
                    {
                        env(accept(borrower, loanKey));
                        env.close();
                    }

                    BEAST_EXPECT(env.le(mptoken) != nullptr);
                },
                {},
                CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1, .flow = flow});
        }

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                {},
                [&, flow](Env& env, BrokerInfo const& broker) {
                    using namespace loan;
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "IOU authorized borrower, lender has no "
                                "reserve ("
                             << flowLabel << ")";
                    // Remove trust line from lender to issuer
                    env.trust(broker.asset(0), lender);
                    env.close();

                    auto const trustline =
                        keylet::trustLine(lender, broker.asset.raw().get<Issue>());
                    auto const sleLine1 = env.le(trustline);
                    BEAST_EXPECT(sleLine1 != nullptr);

                    env(pay(lender, issuer, broker.asset(abs(sleLine1->at(sfBalance).value()))));
                    env.close();
                    BEAST_EXPECT(env.le(trustline) == nullptr);

                    // Burn some XRP
                    env(noop(lender), Fee(XRP(incReserve)));
                    env.close();

                    // One-step: addEmptyHolding on the trust line returns
                    // tecNO_LINE_INSUF_RESERVE. Two-step: reserveLoanOwner on
                    // the pending loan returns the generic
                    // tecINSUFFICIENT_RESERVE before disbursement is reached.
                    TER const expected = flow == LoanFlow::OneStep ? TER{tecNO_LINE_INSUF_RESERVE}
                                                                   : TER{tecINSUFFICIENT_RESERVE};
                    submitSet(
                        env,
                        flow,
                        broker,
                        borrower,
                        lender,
                        principalRequest,
                        kLoanOriginationFee(broker.asset(1).value()),
                        Ter{expected});
                    env.close();

                    // Top up the lender and retry.
                    env(pay(issuer, lender, XRP(incReserve)));
                    env.close();
                    auto const loanKey = submitSet(
                        env,
                        flow,
                        broker,
                        borrower,
                        lender,
                        principalRequest,
                        kLoanOriginationFee(broker.asset(1).value()));
                    env.close();

                    if (flow == LoanFlow::TwoStep)
                    {
                        env(accept(borrower, loanKey));
                        env.close();
                    }

                    BEAST_EXPECT(env.le(trustline) != nullptr);
                },
                CaseArgs{.initialXRP = (acctReserve * 2) + (incReserve * 8) + 1, .flow = flow});
        }

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                [&, flow](Env& env, BrokerInfo const& broker, MPTTester& mptt) {
                    using namespace loan;
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "MPT authorized borrower, unauthorized lender (" << flowLabel
                             << ")";
                    auto const mptoken = keylet::mptoken(mptt.issuanceID(), lender);
                    auto const sleMPT1 = env.le(mptoken);
                    BEAST_EXPECT(sleMPT1 != nullptr);

                    env(pay(lender, issuer, broker.asset(sleMPT1->at(sfMPTAmount))));
                    env.close();

                    mptt.authorize({.account = lender, .flags = tfMPTUnauthorize});
                    env.close();

                    BEAST_EXPECT(env.le(mptoken) == nullptr);

                    // Cannot create loan, lender not authorized to receive fee
                    submitSet(
                        env,
                        flow,
                        broker,
                        borrower,
                        lender,
                        principalRequest,
                        kLoanOriginationFee(broker.asset(1).value()),
                        Ter{tecNO_AUTH});
                    env.close();

                    // Cannot create loan, even without an origination fee
                    submitSet(
                        env, flow, broker, borrower, lender, principalRequest, Ter{tecNO_AUTH});
                    env.close();

                    // No MPToken for lender - no authorization and no payment
                    BEAST_EXPECT(env.le(mptoken) == nullptr);
                },
                {},
                CaseArgs{.requireAuth = true, .authorizeBorrower = true, .flow = flow});
        }

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                [&, flow](Env& env, BrokerInfo const& broker, auto&) {
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "MPT authorized borrower, borrower submits (" << flowLabel << ")";
                    submitSet(env, flow, broker, borrower, lender, principalRequest);
                },
                [&, flow](Env& env, BrokerInfo const& broker) {
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "IOU authorized borrower, borrower submits (" << flowLabel << ")";
                    submitSet(env, flow, broker, borrower, lender, principalRequest);
                },
                CaseArgs{.requireAuth = true, .authorizeBorrower = true, .flow = flow});
        }

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                [&, flow](Env& env, BrokerInfo const& broker, auto&) {
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "MPT authorized borrower, lender submits (" << flowLabel << ")";
                    submitSet(env, flow, broker, lender, borrower, principalRequest);
                },
                [&, flow](Env& env, BrokerInfo const& broker) {
                    Number const principalRequest = broker.asset(1'000).value();

                    testcase << "IOU authorized borrower, lender submits (" << flowLabel << ")";
                    submitSet(env, flow, broker, lender, borrower, principalRequest);
                },
                CaseArgs{.requireAuth = true, .authorizeBorrower = true, .flow = flow});
        }

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

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                [&, flow](Env& env, BrokerInfo const& broker, auto&) {
                    using namespace loan;
                    Number const principalRequest = broker.asset(1'000).value();
                    Vault const vault{env};
                    auto tx = vault.set({.owner = lender, .id = broker.vaultID});
                    tx[sfAssetsMaximum] = BrokerParameters::defaults().vaultDeposit;
                    env(tx);
                    env.close();

                    testcase << "Vault at maximum value (" << flowLabel << ")";
                    submitSet(
                        env,
                        flow,
                        broker,
                        issuer,
                        lender,
                        principalRequest,
                        kInterestRate(TenthBips32(10'000)),
                        Ter(tecLIMIT_EXCEEDED));
                },
                nullptr,
                CaseArgs{.flow = flow});
        }

        for (auto const flow : {LoanFlow::OneStep, LoanFlow::TwoStep})
        {
            char const* const flowLabel = flow == LoanFlow::OneStep ? "one-step" : "two-step";
            testCase(
                [&, flow](Env& env, BrokerInfo const& broker, auto&) {
                    using namespace loan;
                    Number const principalRequest = broker.asset(1'000).value();
                    Vault const vault{env};
                    auto tx = vault.set({.owner = lender, .id = broker.vaultID});
                    tx[sfAssetsMaximum] =
                        BrokerParameters::defaults().vaultDeposit + broker.asset(1).number();
                    env(tx);
                    env.close();

                    testcase << "Vault maximum value exceeded (" << flowLabel << ")";
                    submitSet(
                        env,
                        flow,
                        broker,
                        issuer,
                        lender,
                        principalRequest,
                        kInterestRate(TenthBips32(100'000)),
                        kPaymentTotal(2),
                        kPaymentInterval(3600 * 24),
                        Ter(tecLIMIT_EXCEEDED));
                },
                nullptr,
                CaseArgs{.flow = flow});
        }
    }

    // LoanSet in a closed-ended vault — phase gating and maturity bound.
    void
    testLoanSetClosedEnded()
    {
        testcase("LoanSet closed-ended: phase and maturity bound");
        using namespace jtx;
        using namespace loan;

        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};

        // Common loan schedule used by the phase-rejection cases below.
        constexpr std::uint32_t kInterval = 3600u * 24u;  // 1 day
        constexpr std::uint32_t kTotal = 2u;

        // featureLendingProtocolV1_1 is excluded from `all_` by convention (see the comment on
        // `all_`), so callers must opt in. Closed-ended vaults are gated on this amendment; without
        // it VaultCreate returns temDISABLED and every follow-on txn sees tecNO_ENTRY.
        auto const withEnv = [&, this](auto&& body) {
            Env env(*this, testableAmendments() | featureLendingProtocolV1_1);
            env.fund(XRP(1'000'000'000), issuer, lender, borrower);
            env.close();
            PrettyAsset const asset{xrpIssue(), 1'000'000};
            body(env, asset);
        };

        auto const setLoan = [&](Env& env, BrokerInfo const& broker, TER expected) {
            env(set(lender, broker.brokerID, broker.asset(100).value()),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                Fee(env.current()->fees().base * 5),
                kPaymentTotal(kTotal),
                kPaymentInterval(kInterval),
                Ter(expected));
            env.close();
        };

        // 1. Rejected during Subscription: the broker is created in Subscription (skipPhaseAdvance
        // = true), then LoanSet is attempted before advancing past SubscriptionDate.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            auto const broker = createVaultAndBroker(
                env,
                asset,
                lender,
                BrokerParameters{.vaultKind = VaultKind::ClosedEnded, .skipPhaseAdvance = true});
            setLoan(env, broker, tecTOO_SOON);
        });

        // 2. Rejected during Redemption: broker is set up normally (which lands the vault in
        // Investment), then advance the clock past RedemptionDate before attempting LoanSet.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            auto const broker = createVaultAndBroker(
                env, asset, lender, BrokerParameters{.vaultKind = VaultKind::ClosedEnded});
            BEAST_EXPECT(broker.redemptionDate.has_value());
            using d = NetClock::duration;
            using tp = NetClock::time_point;
            env.close(tp{d{*broker.redemptionDate + 1}});
            setLoan(env, broker, tecEXPIRED);
        });

        // 3. Accepted during Investment when the schedule comfortably fits before RedemptionDate.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            auto const broker = createVaultAndBroker(
                env, asset, lender, BrokerParameters{.vaultKind = VaultKind::ClosedEnded});
            setLoan(env, broker, tesSUCCESS);
        });

        // 4. Rejected during Investment when the loan's final payment would land on or after
        // RedemptionDate. Use a tight redemptionOffset and a schedule whose final payment is well
        // past that boundary.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            constexpr std::uint32_t kRedemptionOffset = 3u * 24u * 3600u;
            auto const broker = createVaultAndBroker(
                env,
                asset,
                lender,
                BrokerParameters{
                    .vaultKind = VaultKind::ClosedEnded, .redemptionOffset = kRedemptionOffset});
            env(set(lender, broker.brokerID, broker.asset(100).value()),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                Fee(env.current()->fees().base * 5),
                kPaymentTotal(10u),
                kPaymentInterval(kInterval),
                Ter(tecNO_PERMISSION));
            env.close();
        });

        // 5. Boundary: schedule whose finalPayment lands exactly (RedemptionDate - 1) is accepted,
        // and one second later (== RedemptionDate) is rejected. Uses payTotal = 1 so the arithmetic
        // is simple: finalPayment = startDate + interval.
        withEnv([&](Env& env, PrettyAsset const& asset) {
            auto const broker = createVaultAndBroker(
                env, asset, lender, BrokerParameters{.vaultKind = VaultKind::ClosedEnded});
            BEAST_EXPECT(broker.redemptionDate.has_value());

            auto const startDate = env.now().time_since_epoch().count();
            auto const acceptInterval = *broker.redemptionDate - 1 - startDate;
            env(set(lender, broker.brokerID, broker.asset(100).value()),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                Fee(env.current()->fees().base * 5),
                kPaymentTotal(1u),
                kPaymentInterval(acceptInterval),
                Ter(tesSUCCESS));
            env.close();

            auto const rejectInterval =
                *broker.redemptionDate - env.now().time_since_epoch().count();
            env(set(lender, broker.brokerID, broker.asset(100).value()),
                kCounterparty(borrower),
                Sig(sfCounterpartySignature, borrower),
                Fee(env.current()->fees().base * 5),
                kPaymentTotal(1u),
                kPaymentInterval(rejectInterval),
                Ter(tecNO_PERMISSION));
            env.close();
        });
    }

public:
    void
    run() override
    {
        for (auto const& features : jtx::amendmentCombinations(
                 {fixCleanup3_1_3, fixCleanup3_2_0, featureMPTokensV2, featureLendingProtocolV1_1},
                 all_))
            testLoanSet(features);

        testLoanSetClosedEnded();
    }
};

BEAST_DEFINE_TESTSUITE(LoanSet, tx, xrpl);

}  // namespace xrpl::test
