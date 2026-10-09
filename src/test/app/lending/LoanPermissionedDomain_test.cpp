#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/fee.h>
#include <test/jtx/pay.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace xrpl::test {

// LoanSet and LoanPay behaviour for private loan brokers that gate borrowers
// through a permissioned domain.
class LoanPermissionedDomain_test : public LoanTestBase
{
    std::string const credType_{"LoanCredential"};

    // Funds `account` and gives it a trust line and a balance of `asset`
    static void
    fundBorrower(
        jtx::Env& env,
        jtx::Account const& issuer,
        jtx::Account const& account,
        jtx::PrettyAsset const& asset)
    {
        using namespace jtx;

        env.fund(XRP(1'000'000), account);
        env.close();
        env(trust(account, asset(1'000'000'000)));
        env(pay(issuer, account, asset(1'000'000)));
        env.close();
    }

    // Issues a `credType_` credential from `credIssuer` to `subject`, and has
    // the subject accept it
    void
    issueCredential(jtx::Env& env, jtx::Account const& subject, jtx::Account const& credIssuer)
    {
        using namespace jtx;

        env(credentials::create(subject, credIssuer, credType_));
        env.close();
        env(credentials::accept(subject, credIssuer, credType_));
        env.close();
    }

    // Build a LoanSet with fixed terms, signed by the counterparty
    static jtx::JTx
    makeLoanSet(
        jtx::Env& env,
        jtx::Account const& submitter,
        BrokerInfo const& broker,
        jtx::Account const& counterparty)
    {
        using namespace jtx;
        auto setTx = env.jt(loan::set(submitter, broker.brokerID, broker.asset(100).number()));
        Sig(sfCounterpartySignature, counterparty)(env, setTx);
        Fee{env.current()->fees().base * 2}(env, setTx);
        loan::kCounterparty(counterparty)(env, setTx);
        loan::kInterestRate(TenthBips32{1000})(env, setTx);
        loan::kPaymentTotal(2)(env, setTx);
        loan::kPaymentInterval(100)(env, setTx);
        return setTx;
    }

    // Has `borrower` make one loan payment and checks that it was genuinely
    // applied, not merely that the loan object survives: the borrower's balance
    // dropped (fees are charged separately in XRP), one payment period was
    // consumed, and the loan's outstanding figures decreased.
    void
    payLoanAndVerify(
        jtx::Env& env,
        jtx::Account const& borrower,
        Keylet const& loanKeylet,
        jtx::PrettyAsset const& asset)
    {
        using namespace jtx;

        auto const sleLoanBefore = env.le(loanKeylet);
        if (!BEAST_EXPECT(sleLoanBefore))
            return;
        auto const balanceBefore = env.balance(borrower, asset).value();
        auto const paymentRemainingBefore = sleLoanBefore->at(sfPaymentRemaining);
        auto const principalBefore = sleLoanBefore->at(sfPrincipalOutstanding);
        auto const totalValueBefore = sleLoanBefore->at(sfTotalValueOutstanding);

        env(loan::pay(borrower, loanKeylet.key, asset(51)));
        env.close();

        auto const sleLoan = env.le(loanKeylet);
        if (!BEAST_EXPECT(sleLoan))
            return;
        BEAST_EXPECT(env.balance(borrower, asset).value() < balanceBefore);
        BEAST_EXPECT(sleLoan->at(sfPaymentRemaining) == paymentRemainingBefore - 1);
        BEAST_EXPECT(sleLoan->at(sfPrincipalOutstanding) < principalBefore);
        BEAST_EXPECT(sleLoan->at(sfTotalValueOutstanding) < totalValueBefore);
    }

    void
    testPrivateBrokerDomainStates()
    {
        testcase("Private broker gates new loans on its permissioned domain");
        using namespace jtx;
        using namespace loan_broker;

        Account const issuer{"issuer"};
        Account const alice{"alice"};  // Broker owner
        Account const bob{"bob"};      // Borrower with credentials
        Account const carol{"carol"};  // Borrower without credentials
        Account const credIssuer{"credIssuer"};

        // DomainID and private Loan Brokers require featureLendingProtocolV1_2.
        Env env{*this, all_ | featureLendingProtocolV1_2};

        auto const asset = createFundedIouAsset(env, issuer, alice, bob);
        fundBorrower(env, issuer, carol, asset);
        env.fund(XRP(1'000'000), credIssuer);
        env.close();

        auto const domainId = createDomain(env, credIssuer, credIssuer, credType_);

        // Only bob gets credentials; carol does not
        issueCredential(env, bob, credIssuer);

        auto const broker = createVaultAndBroker(
            env, asset, alice, {.flags = tfLoanBrokerPrivate, .domainID = domainId});

        auto const setBrokerDomain = [&](uint256 const& newDomainId) {
            env(set(alice, broker.vaultID), kLoanBrokerId(broker.brokerID), kDomainId(newDomainId));
            env.close();
        };

        // Each step moves the broker (or its domain) into a new state, which
        // later steps build on
        struct Step
        {
            std::string name;
            std::function<void()> changeState;
            std::optional<uint256> expectedDomainId;
            TER memberResult;
            TER nonMemberResult;
        };

        std::vector<Step> const steps{
            {.name = "DomainID set",
             .changeState = {},
             .expectedDomainId = domainId,
             .memberResult = tesSUCCESS,
             .nonMemberResult = tecNO_AUTH},
            {.name = "DomainID unset",
             .changeState = [&] { setBrokerDomain(beast::kZero); },
             .expectedDomainId = std::nullopt,
             .memberResult = tecNO_AUTH,
             .nonMemberResult = tecNO_AUTH},
            {.name = "DomainID set again",
             .changeState = [&] { setBrokerDomain(domainId); },
             .expectedDomainId = domainId,
             .memberResult = tesSUCCESS,
             .nonMemberResult = tecNO_AUTH},
            {.name = "Domain deleted while the broker still refers to it",
             .changeState =
                 [&] {
                     env(pdomain::deleteTx(credIssuer, domainId));
                     env.close();
                     BEAST_EXPECT(!env.le(keylet::permissionedDomain(domainId)));
                 },
             .expectedDomainId = domainId,
             .memberResult = tecOBJECT_NOT_FOUND,
             .nonMemberResult = tecOBJECT_NOT_FOUND},
            {.name = "Stale DomainID cleared by the broker owner",
             .changeState = [&] { setBrokerDomain(beast::kZero); },
             .expectedDomainId = std::nullopt,
             .memberResult = tecNO_AUTH,
             .nonMemberResult = tecNO_AUTH},
        };

        // Submits a LoanSet for `borrower` and checks a loan is created only
        // on success. Returns the new loan's keylet, if any.
        auto const trySetLoan = [&](Account const& borrower,
                                    TER expected) -> std::optional<Keylet> {
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, borrower, broker, alice), Ter(expected));
            env.close();

            bool const created = isTesSuccess(expected);
            BEAST_EXPECT(static_cast<bool>(env.le(loanKeylet)) == created);
            if (!created)
                return std::nullopt;
            return loanKeylet;
        };

        // A loan bob took out in an earlier step that is yet to be repaid
        std::optional<Keylet> outstandingLoan;
        for (auto const& step : steps)
        {
            testcase(step.name);

            if (step.changeState)
                step.changeState();

            if (auto const sleBroker = env.le(broker.brokerKeylet()); BEAST_EXPECT(sleBroker))
            {
                BEAST_EXPECT(sleBroker->at(~sfDomainID) == step.expectedDomainId);
                BEAST_EXPECT(sleBroker->isFlag(lsfLoanBrokerPrivate));
            }

            // Existing loans can be repaid whatever state the domain is in
            if (outstandingLoan)
                payLoanAndVerify(env, bob, *outstandingLoan, asset);

            trySetLoan(carol, step.nonMemberResult);
            outstandingLoan = trySetLoan(bob, step.memberResult);
        }
    }

    void
    testPrivateBrokerCredentials()
    {
        testcase("Private Broker Credential Validation");
        using namespace jtx;

        Account const issuer{"issuer"};
        Account const alice{"alice"};  // Broker owner
        Account const bob{"bob"};      // Borrower with credentials
        Account const carol{"carol"};  // Borrower without credentials
        Account const credIssuer{"credIssuer"};

        // DomainID and private Loan Brokers require featureLendingProtocolV1_2.
        Env env{*this, all_ | featureLendingProtocolV1_2};

        auto const asset = createFundedIouAsset(env, issuer, alice, bob);
        fundBorrower(env, issuer, carol, asset);
        env.fund(XRP(1'000'000), credIssuer);
        env.close();

        auto const domainId = createDomain(env, credIssuer, credIssuer, credType_);

        auto const broker = createVaultAndBroker(
            env, asset, alice, {.flags = tfLoanBrokerPrivate, .domainID = domainId});

        {
            testcase("Borrower without credentials cannot create loan");
            env(makeLoanSet(env, carol, broker, alice), Ter(tecNO_AUTH));
        }

        issueCredential(env, bob, credIssuer);

        {
            testcase("Borrower with valid credentials can create loan");
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, bob, broker, alice));
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
        }

        {
            testcase("Private broker without DomainID configured rejects loans");
            auto const noDomainBroker =
                createVaultAndBroker(env, asset, alice, {.flags = tfLoanBrokerPrivate});

            // Even bob with credentials should fail - broker has no domain configured
            env(makeLoanSet(env, bob, noDomainBroker, alice), Ter(tecNO_AUTH));
        }

        {
            testcase("Public broker allows anyone to create loan");
            auto const publicBroker = createVaultAndBroker(env, asset, alice);

            // Carol without credentials can create loan on public broker
            auto const loanKeylet = nextLoanKeylet(env, publicBroker);
            env(makeLoanSet(env, carol, publicBroker, alice));
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
        }

        {
            testcase(
                "When the broker owner submits, the domain check must validate the borrower (the "
                "counterparty)");
            env(makeLoanSet(env, alice, broker, carol), Ter(tecNO_AUTH));
        }

        {
            testcase("Broker owner submits with a credentialed borrower (bob) as counterparty.");
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, alice, broker, bob));
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
        }

        {
            testcase("Broker owner outside the domain cannot take a loan from their own broker");
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, alice, broker, alice), Ter(tecNO_AUTH));
            env.close();

            BEAST_EXPECT(!env.le(loanKeylet));
        }

        {
            testcase("Borrower whose only credential has expired");
            using namespace std::chrono_literals;

            auto jv = credentials::create(carol, credIssuer, credType_);
            std::uint32_t const expiration =
                env.current()->header().parentCloseTime.time_since_epoch().count() + 100;
            jv[sfExpiration.jsonName] = expiration;
            env(jv);
            env(credentials::accept(carol, credIssuer, credType_));
            env.close();

            auto const credKeylet = credentials::keylet(carol, credIssuer, credType_);

            // Advance time past expiration
            env.close(150s);
            BEAST_EXPECT(env.le(credKeylet));

            env(makeLoanSet(env, carol, broker, alice), Ter(tecEXPIRED));
            env.close();

            // The expired credential was deleted despite the tec result
            BEAST_EXPECT(!env.le(credKeylet));

            // With the credential gone, the borrower is rejected outright
            env(makeLoanSet(env, carol, broker, alice), Ter(tecNO_AUTH));
            env.close();
        }
    }

    void
    testPrivateBrokerDomainOwner()
    {
        testcase("Private broker does not treat the domain owner as a member");
        using namespace jtx;

        Account const issuer{"issuer"};
        Account const alice{"alice"};  // Broker owner
        Account const bob{"bob"};      // Domain owner
        Account const carol{"carol"};  // Neither owner nor member
        Account const credIssuer{"credIssuer"};

        // DomainID and private Loan Brokers require featureLendingProtocolV1_2.
        Env env{*this, all_ | featureLendingProtocolV1_2};

        auto const asset = createFundedIouAsset(env, issuer, alice, bob);
        fundBorrower(env, issuer, carol, asset);
        env.fund(XRP(1'000'000), credIssuer);
        env.close();

        // Bob owns the domain but holds no credential in it yet
        auto const domainId = createDomain(env, bob, credIssuer, credType_);
        BEAST_EXPECT(!env.le(credentials::keylet(bob, credIssuer, credType_)));

        // Alice creates a private broker attached to bob's domain
        auto const broker = createVaultAndBroker(
            env, asset, alice, {.flags = tfLoanBrokerPrivate, .domainID = domainId});

        {
            testcase("Owning the domain does not make the owner a member");
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, bob, broker, alice), Ter(tecNO_AUTH));
            env.close();

            BEAST_EXPECT(!env.le(loanKeylet));
        }

        {
            testcase("The domain owner is not a member as the counterparty either");
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, alice, broker, bob), Ter(tecNO_AUTH));
            env.close();

            BEAST_EXPECT(!env.le(loanKeylet));
        }

        {
            testcase("Anyone else without a credential is rejected the same way");
            env(makeLoanSet(env, carol, broker, alice), Ter(tecNO_AUTH));
        }

        // The domain owner can join the domain like anyone else
        issueCredential(env, bob, credIssuer);

        {
            testcase("The domain owner can borrow once they hold a credential");
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, bob, broker, alice));
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
        }

        {
            testcase("The credentialed domain owner can also borrow as the counterparty");
            auto const loanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, alice, broker, bob));
            env.close();

            BEAST_EXPECT(env.le(loanKeylet));
        }

        {
            testcase("Broker owner who also owns the domain");
            auto const aliceDomainId = createDomain(env, alice, credIssuer, credType_);
            auto const aliceBroker = createVaultAndBroker(
                env, asset, alice, {.flags = tfLoanBrokerPrivate, .domainID = aliceDomainId});

            // Being the domain owner does not let alice lend to a non-member
            env(makeLoanSet(env, alice, aliceBroker, carol), Ter(tecNO_AUTH));

            // Nor does it let alice take a self-loan without a credential
            {
                auto const loanKeylet = nextLoanKeylet(env, aliceBroker);
                env(makeLoanSet(env, alice, aliceBroker, alice), Ter(tecNO_AUTH));
                env.close();

                BEAST_EXPECT(!env.le(loanKeylet));
            }

            // Once alice holds a credential in her own domain, the self-loan
            // goes through
            issueCredential(env, alice, credIssuer);
            {
                auto const loanKeylet = nextLoanKeylet(env, aliceBroker);
                env(makeLoanSet(env, alice, aliceBroker, alice));
                env.close();

                BEAST_EXPECT(env.le(loanKeylet));
            }
        }

        {
            testcase("Once the domain is deleted, the former owner gets tecOBJECT_NOT_FOUND");
            env(pdomain::deleteTx(bob, domainId));
            env.close();
            BEAST_EXPECT(!env.le(keylet::permissionedDomain(domainId)));

            // Bob still holds a credential, but the broker's domain is gone
            BEAST_EXPECT(env.le(credentials::keylet(bob, credIssuer, credType_)));
            env(makeLoanSet(env, bob, broker, alice), Ter(tecOBJECT_NOT_FOUND));
        }
    }

    void
    testPrivateBrokerLoanPayAfterCredentialRevoked()
    {
        testcase("LoanPay works after borrower credential is revoked");
        using namespace jtx;

        Account const issuer{"issuer"};
        Account const alice{"alice"};  // Broker owner
        Account const bob{"bob"};      // Borrower
        Account const credIssuer{"credIssuer"};

        // DomainID and private Loan Brokers require featureLendingProtocolV1_2.
        Env env{*this, all_ | featureLendingProtocolV1_2};

        auto const asset = createFundedIouAsset(env, issuer, alice, bob);
        env.fund(XRP(1'000'000), credIssuer);
        env.close();

        auto const domainId = createDomain(env, credIssuer, credIssuer, credType_);
        issueCredential(env, bob, credIssuer);

        auto const broker = createVaultAndBroker(
            env, asset, alice, {.flags = tfLoanBrokerPrivate, .domainID = domainId});

        // Create a loan for bob
        auto const loanKeylet = nextLoanKeylet(env, broker);
        env(makeLoanSet(env, bob, broker, alice));
        env.close();
        BEAST_EXPECT(env.le(loanKeylet));

        // Now delete bob's credential
        env(credentials::deleteCred(bob, bob, credIssuer, credType_));
        env.close();

        // Verify credential is gone
        auto const credKeylet = credentials::keylet(bob, credIssuer, credType_);
        BEAST_EXPECT(!env.le(credKeylet));

        // Bob should still be able to make a loan payment even without credentials
        payLoanAndVerify(env, bob, loanKeylet, asset);

        // Bob should NOT be able to create a NEW loan (no credentials)
        {
            auto const newLoanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, bob, broker, alice), Ter(tecNO_AUTH));

            BEAST_EXPECT(!env.le(newLoanKeylet));
        }
    }

    void
    testPrivateBrokerLoanPayAfterCredentialExpired()
    {
        testcase("LoanPay works after borrower credential has expired");
        using namespace jtx;
        using namespace std::chrono_literals;

        Account const issuer{"issuer"};
        Account const alice{"alice"};  // Broker owner
        Account const bob{"bob"};      // Borrower
        Account const credIssuer{"credIssuer"};

        // DomainID and private Loan Brokers require featureLendingProtocolV1_2.
        Env env{*this, all_ | featureLendingProtocolV1_2};

        auto const asset = createFundedIouAsset(env, issuer, alice, bob);
        env.fund(XRP(1'000'000), credIssuer);
        env.close();

        auto const domainId = createDomain(env, credIssuer, credIssuer, credType_);
        auto const broker = createVaultAndBroker(
            env, asset, alice, {.flags = tfLoanBrokerPrivate, .domainID = domainId});

        // Give bob a credential that expires shortly, well before the first
        // loan payment falls due
        auto jv = credentials::create(bob, credIssuer, credType_);
        std::uint32_t const expiration =
            env.current()->header().parentCloseTime.time_since_epoch().count() + 20;
        jv[sfExpiration.jsonName] = expiration;
        env(jv);
        env(credentials::accept(bob, credIssuer, credType_));
        env.close();

        // Create a loan for bob while the credential is still valid
        auto const loanKeylet = nextLoanKeylet(env, broker);
        env(makeLoanSet(env, bob, broker, alice));
        env.close();
        BEAST_EXPECT(env.le(loanKeylet));

        // Advance time past expiration
        env.close(30s);
        auto const credKeylet = credentials::keylet(bob, credIssuer, credType_);
        BEAST_EXPECT(env.le(credKeylet));

        // Bob can still make a loan payment with an expired credential
        payLoanAndVerify(env, bob, loanKeylet, asset);

        // LoanPay does not check the domain, so it leaves the expired
        // credential in place
        BEAST_EXPECT(env.le(credKeylet));

        // Bob cannot create a new loan with the expired credential
        {
            auto const newLoanKeylet = nextLoanKeylet(env, broker);
            env(makeLoanSet(env, bob, broker, alice), Ter(tecEXPIRED));
            env.close();

            BEAST_EXPECT(!env.le(newLoanKeylet));
        }
    }

public:
    void
    run() override
    {
        testPrivateBrokerDomainStates();
        testPrivateBrokerCredentials();
        testPrivateBrokerDomainOwner();
        testPrivateBrokerLoanPayAfterCredentialRevoked();
        testPrivateBrokerLoanPayAfterCredentialExpired();
    }
};

BEAST_DEFINE_TESTSUITE(LoanPermissionedDomain, tx, xrpl);

}  // namespace xrpl::test
