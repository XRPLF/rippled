#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/acctdelete.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/transactors/lending/LoanSet.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace xrpl::test {

/**
 * AccountLeasing: a lease loan keeps its principal on its own pseudo-account
 * (the lease account), which sponsors the reserves of the accounts the
 * borrower creates with it. See account-leasing/specs/xls-account-leasing.md.
 */
class AccountLeasing_test : public LoanTestBase
{
    // One XRP vault, one broker, and the parties of a lease.
    struct Lease
    {
        jtx::Account lender{"lender"};
        jtx::Account borrower{"borrower"};
        jtx::Account stranger{"stranger"};
        BrokerInfo broker;
        Keylet loanKeylet{ltLOAN, beast::kZero};
        XRPAmount reserve{0};
        XRPAmount principal{0};
        AccountID leaseAccount;
    };

    static jtx::PrettyAsset
    xrpAsset()
    {
        return jtx::PrettyAsset{xrpIssue(), 1'000'000};
    }

    // Fund the parties and create the XRP vault and its broker.
    Lease
    setupBroker(jtx::Env& env)
    {
        using namespace jtx;
        Account const lender{"lender"};
        env.fund(XRP(10'000'000), lender);
        env.fund(XRP(100'000), Account{"borrower"}, Account{"stranger"});
        env.close();
        Lease lease{.broker = createVaultAndBroker(env, xrpAsset(), lender)};
        lease.reserve = env.current()->fees().reserve;
        return lease;
    }

    // The next Loan's keylet on the broker.
    static Keylet
    nextLoanKeylet(jtx::Env& env, BrokerInfo const& broker)
    {
        auto const brokerSle = env.le(keylet::loanBroker(broker.brokerID));
        return keylet::loan(broker.brokerID, SeqProxy::rawSequence(brokerSle->at(sfLoanSequence)));
    }

    // Create a lease loan holding `units` base reserves.
    void
    createLease(jtx::Env& env, Lease& lease, std::int64_t units, TER expected = tesSUCCESS)
    {
        using namespace jtx;
        using namespace loan;
        lease.principal = XRPAmount{lease.reserve.drops() * units};
        lease.loanKeylet = nextLoanKeylet(env, lease.broker);
        env(set(lease.borrower,
                lease.broker.brokerID,
                Number{lease.principal.drops()},
                tfLoanLease),
            kCounterparty(lease.lender),
            Sig(sfCounterpartySignature, lease.lender),
            Fee(env.current()->fees().base * 5),
            Ter(expected));
        env.close();
        if (auto const loanSle = env.le(lease.loanKeylet); loanSle && loanSle->at(~sfAccount))
            lease.leaseAccount = *loanSle->at(~sfAccount);
    }

    // Create a leased account holding `amount`.
    static void
    createLeased(
        jtx::Env& env,
        Lease const& lease,
        jtx::Account const& user,
        STAmount const& amount,
        TER expected = tesSUCCESS)
    {
        using namespace jtx;
        env(pay(lease.borrower, user, amount),
            Txflags(tfSponsorCreatedAccount),
            Json(sfLoanID.jsonName, to_string(lease.loanKeylet.key)),
            Fee(XRP(1)),
            Ter(expected));
        env.close();
    }

    std::uint32_t
    leaseCount(jtx::Env& env, Lease const& lease)
    {
        auto const sle = env.le(keylet::account(lease.leaseAccount));
        if (!BEAST_EXPECT(sle))
            return 0;
        return sle->at(sfSponsoringAccountCount);
    }

    XRPAmount
    leaseBalance(jtx::Env& env, Lease const& lease)
    {
        auto const sle = env.le(keylet::account(lease.leaseAccount));
        if (!BEAST_EXPECT(sle))
            return XRPAmount{0};
        return sle->at(sfBalance).value().xrp();
    }

    XRPAmount
    accountBalance(jtx::Env& env, AccountID const& id)
    {
        auto const sle = env.le(keylet::account(id));
        if (!BEAST_EXPECT(sle))
            return XRPAmount{0};
        return sle->at(sfBalance).value().xrp();
    }

    // Move the clock past the loan's due date and grace period.
    static void
    passGracePeriod(jtx::Env& env)
    {
        using namespace std::chrono_literals;
        env.close(
            env.now() +
            std::chrono::seconds{LoanSet::kDefaultPaymentInterval + LoanSet::kDefaultGracePeriod} +
            10s);
    }

    void
    testDisabled(FeatureBitset features)
    {
        testcase("Disabled without AccountLeasing");
        using namespace jtx;
        using namespace loan;

        Env env{*this, features - featureAccountLeasing};
        Lease lease = setupBroker(env);
        Account const user{"user"};
        env.memoize(user);

        // LoanSet with tfLoanLease.
        createLease(env, lease, 3, temINVALID_FLAG);
        BEAST_EXPECT(!env.le(lease.loanKeylet));

        // Payment with LoanID.
        env(pay(lease.borrower, user, XRP(10)),
            Txflags(tfSponsorCreatedAccount),
            Json(sfLoanID.jsonName, to_string(lease.loanKeylet.key)),
            Fee(XRP(1)),
            Ter(temDISABLED));

        // LoanManage with tfLoanLeaseRecover.
        env(manage(lease.lender, lease.loanKeylet.key, tfLoanLeaseRecover), Ter(temINVALID_FLAG));

        // SponsorshipTransfer with tfSponsorshipReap.
        env(sponsor::transfer(lease.borrower, tfSponsorshipReap),
            sponsor::SponseeAcc(lease.stranger),
            Ter(temINVALID_FLAG));
    }

    void
    testLeaseCreate(FeatureBitset features)
    {
        testcase("LoanSet with tfLoanLease");
        using namespace jtx;
        using namespace loan;

        Env env{*this, features};
        Lease lease = setupBroker(env);

        auto const borrowerOwnersBefore = env.ownerCount(lease.borrower);
        auto const borrowerBalanceBefore = env.balance(lease.borrower);

        // Less than one base reserve: refused.
        {
            auto const keylet = nextLoanKeylet(env, lease.broker);
            env(set(lease.borrower,
                    lease.broker.brokerID,
                    Number{lease.reserve.drops() - 1},
                    tfLoanLease),
                kCounterparty(lease.lender),
                Sig(sfCounterpartySignature, lease.lender),
                Fee(env.current()->fees().base * 5),
                Ter(tecINSUFFICIENT_RESERVE));
            env.close();
            BEAST_EXPECT(!env.le(keylet));
        }

        createLease(env, lease, 3);
        auto const loanSle = env.le(lease.loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        BEAST_EXPECT(loanSle->isFlag(lsfLoanLease));
        BEAST_EXPECT(loanSle->at(~sfLeaseUnit) == Number{lease.reserve.drops()});
        BEAST_EXPECT(loanSle->at(sfLeaseClaimVault) == beast::kZero);

        // The lease account holds the principal; the borrower got nothing.
        auto const leaseSle = env.le(keylet::account(lease.leaseAccount));
        if (!BEAST_EXPECT(leaseSle))
            return;
        BEAST_EXPECT(leaseSle->at(sfLoanID) == lease.loanKeylet.key);
        BEAST_EXPECT(leaseSle->isFlag(lsfDisableMaster));
        BEAST_EXPECTS(
            leaseBalance(env, lease) == lease.principal, to_string(leaseBalance(env, lease)));
        BEAST_EXPECT(leaseCount(env, lease) == 0);
        BEAST_EXPECTS(
            env.ownerCount(lease.borrower) == borrowerOwnersBefore + 2,
            std::to_string(env.ownerCount(lease.borrower)));
        BEAST_EXPECT(env.balance(lease.borrower) < borrowerBalanceBefore);
    }

    void
    testLeaseCreateIOU(FeatureBitset features)
    {
        testcase("LoanSet with tfLoanLease on an IOU vault");
        using namespace jtx;
        using namespace loan;

        Env env{*this, features};
        Account const issuer{"issuer"};
        Account const lender{"lender"};
        Account const borrower{"borrower"};
        env.fund(XRP(1'000'000), issuer, lender, borrower);
        env.close();
        PrettyAsset const asset = issuer["IOU"];
        env(trust(lender, asset(10'000'000)));
        env(trust(borrower, asset(10'000'000)));
        env.close();
        env(pay(issuer, lender, asset(5'000'000)));
        env.close();
        auto const broker = createVaultAndBroker(env, asset, lender);
        auto const keylet = nextLoanKeylet(env, broker);
        env(set(borrower, broker.brokerID, asset(100).value(), tfLoanLease),
            kCounterparty(lender),
            Sig(sfCounterpartySignature, lender),
            Fee(env.current()->fees().base * 5),
            Ter(tecWRONG_ASSET));
        env.close();
        BEAST_EXPECT(!env.le(keylet));
    }

    void
    testLeasedAccount(FeatureBitset features)
    {
        testcase("Payment with LoanID");
        using namespace jtx;
        using namespace loan;

        Env env{*this, features};
        Lease lease = setupBroker(env);
        createLease(env, lease, 2);
        Account const user1{"user1"};
        Account const user2{"user2"};
        Account const user3{"user3"};
        env.memoize(user1);
        env.memoize(user2);
        env.memoize(user3);

        // LoanID without tfSponsorCreatedAccount.
        env(pay(lease.borrower, user1, XRP(10)),
            Json(sfLoanID.jsonName, to_string(lease.loanKeylet.key)),
            Ter(temMALFORMED));

        // Not the borrower.
        env(pay(lease.stranger, user1, XRP(10)),
            Txflags(tfSponsorCreatedAccount),
            Json(sfLoanID.jsonName, to_string(lease.loanKeylet.key)),
            Fee(XRP(1)),
            Ter(tecNO_PERMISSION));

        // No such loan.
        env(pay(lease.borrower, user1, XRP(10)),
            Txflags(tfSponsorCreatedAccount),
            Json(
                sfLoanID.jsonName,
                to_string(keylet::loan(lease.broker.brokerID, SeqProxy::rawSequence(999)).key)),
            Fee(XRP(1)),
            Ter(tecNO_ENTRY));

        auto const borrowerCount =
            env.le(keylet::account(lease.borrower))->at(sfSponsoringAccountCount);

        // Two units: two leased accounts, then no room for a third.
        createLeased(env, lease, user1, XRP(1));
        createLeased(env, lease, user2, drops(1));
        createLeased(env, lease, user3, XRP(1), tecINSUFFICIENT_RESERVE);

        auto const user1Sle = env.le(keylet::account(user1));
        if (!BEAST_EXPECT(user1Sle))
            return;
        BEAST_EXPECT(user1Sle->getAccountID(sfSponsor) == lease.leaseAccount);
        BEAST_EXPECT(leaseCount(env, lease) == 2);
        BEAST_EXPECT(leaseBalance(env, lease) == lease.principal);
        BEAST_EXPECT(!env.le(keylet::account(user3)));
        // The borrower sponsors nothing itself.
        BEAST_EXPECT(
            env.le(keylet::account(lease.borrower))->at(sfSponsoringAccountCount) == borrowerCount);
    }

    void
    testRelease(FeatureBitset features)
    {
        testcase("Release of a leased account while the lease is active");
        using namespace jtx;
        using namespace loan;

        Env env{*this, features};
        Lease lease = setupBroker(env);
        createLease(env, lease, 3);
        Account const user1{"user1"};
        Account const user2{"user2"};
        Account const user3{"user3"};
        env.memoize(user1);
        env.memoize(user2);
        env.memoize(user3);
        createLeased(env, lease, user1, XRP(1));
        createLeased(env, lease, user2, XRP(1));
        createLeased(env, lease, user3, drops(1000));
        BEAST_EXPECT(leaseCount(env, lease) == 3);

        // End by a stranger: refused.
        env(sponsor::transfer(lease.stranger, tfSponsorshipEnd),
            sponsor::SponseeAcc(user1),
            Ter(tecNO_PERMISSION));

        // End while the user cannot carry its own reserve: refused.
        env(sponsor::transfer(lease.borrower, tfSponsorshipEnd),
            sponsor::SponseeAcc(user1),
            Ter(tecINSUFFICIENT_RESERVE));
        env.close();

        // Funded, the user is released by the borrower.
        env(pay(lease.stranger, user1, XRP(1'000)));
        env.close();

        // Another sponsor cannot take a leased account over.
        env(sponsor::transfer(user1, tfSponsorshipReassign),
            sponsor::As(lease.stranger, spfSponsorReserve),
            Sig(sfSponsorSignature, lease.stranger),
            Ter(tecNO_PERMISSION));
        env.close();
        env(sponsor::transfer(lease.borrower, tfSponsorshipEnd), sponsor::SponseeAcc(user1));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(user1))->isFieldPresent(sfSponsor));
        BEAST_EXPECT(leaseCount(env, lease) == 2);
        // No XRP moved: the unit is free on the lease account.
        BEAST_EXPECT(leaseBalance(env, lease) == lease.principal);

        // AccountDelete: the residual goes to the borrower, never to the
        // lease account.
        env(pay(lease.stranger, user2, XRP(1'000)));
        env.close();
        incLgrSeqForAccDel(env, user2);
        env(acctdelete(user2, lease.stranger),
            Fee(drops(env.current()->fees().increment)),
            Ter(tecNO_SPONSOR_PERMISSION));
        env.close();
        env(acctdelete(user2, lease.borrower), Fee(drops(env.current()->fees().increment)));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(user2)));
        BEAST_EXPECT(leaseCount(env, lease) == 1);

        // Reap of an abandoned leased account, by the borrower. user3 was
        // created after user2, so a few more ledgers clear its window.
        for (int i = 0; i < 3; ++i)
            env.close();
        env(sponsor::transfer(lease.stranger, tfSponsorshipReap),
            sponsor::SponseeAcc(user3),
            Fee(drops(env.current()->fees().increment)),
            Ter(tecNO_SPONSOR_PERMISSION));
        env(sponsor::transfer(lease.borrower, tfSponsorshipReap),
            sponsor::SponseeAcc(user3),
            Fee(drops(env.current()->fees().increment)));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(user3)));
        BEAST_EXPECT(leaseCount(env, lease) == 0);
        BEAST_EXPECT(leaseBalance(env, lease) == lease.principal);

        // A free unit is used again.
        Account const user4{"user4"};
        env.memoize(user4);
        createLeased(env, lease, user4, XRP(1));
        BEAST_EXPECT(leaseCount(env, lease) == 1);
    }

    void
    testPayoff(FeatureBitset features)
    {
        testcase("Payoff, takeover and LoanDelete");
        using namespace jtx;
        using namespace loan;

        Env env{*this, features};
        Lease lease = setupBroker(env);
        createLease(env, lease, 3);
        Account const user1{"user1"};
        env.memoize(user1);
        createLeased(env, lease, user1, XRP(1));

        // Takeover before payoff: refused.
        env(sponsor::transfer(lease.borrower, tfSponsorshipReassign),
            sponsor::SponseeAcc(user1),
            Ter(tecNO_PERMISSION));

        // Pay the loan off: the free XRP goes to the borrower.
        auto const borrowerBefore = env.balance(lease.borrower);
        auto const loanSle = env.le(lease.loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        STAmount const due{XRPAmount{static_cast<Number>(loanSle->at(sfTotalValueOutstanding))}};
        env(loan::pay(lease.borrower, lease.loanKeylet.key, due), Fee(XRP(1)));
        env.close();
        auto const paidSle = env.le(lease.loanKeylet);
        if (!BEAST_EXPECT(paidSle))
            return;
        BEAST_EXPECT(paidSle->at(sfPaymentRemaining) == 0);
        BEAST_EXPECTS(
            leaseBalance(env, lease) == lease.reserve, to_string(leaseBalance(env, lease)));
        // Paid `due` and a fee of 1 XRP, got two free units back.
        BEAST_EXPECT(
            env.balance(lease.borrower) ==
            borrowerBefore - due - XRP(1) + STAmount{XRPAmount{lease.reserve.drops() * 2}});

        // No new leased account after payoff.
        Account const user2{"user2"};
        env.memoize(user2);
        createLeased(env, lease, user2, XRP(1), tecNO_PERMISSION);

        // LoanDelete while an account is leased: refused.
        env(del(lease.borrower, lease.loanKeylet.key), Ter(tecHAS_OBLIGATIONS));
        env.close();

        // A stranger cannot take over.
        env(sponsor::transfer(lease.stranger, tfSponsorshipReassign),
            sponsor::SponseeAcc(user1),
            Ter(tecNO_PERMISSION));

        // The borrower takes the account over with its unit.
        auto const borrowerCountBefore =
            env.le(keylet::account(lease.borrower))->at(sfSponsoringAccountCount);
        env(sponsor::transfer(lease.borrower, tfSponsorshipReassign), sponsor::SponseeAcc(user1));
        env.close();
        BEAST_EXPECT(
            env.le(keylet::account(user1))->getAccountID(sfSponsor) == lease.borrower.id());
        BEAST_EXPECT(leaseCount(env, lease) == 0);
        BEAST_EXPECT(leaseBalance(env, lease) == XRPAmount{0});
        BEAST_EXPECT(
            env.le(keylet::account(lease.borrower))->at(sfSponsoringAccountCount) ==
            borrowerCountBefore + 1);

        // LoanDelete erases the loan and its lease account.
        auto const ownersBefore = env.ownerCount(lease.borrower);
        env(del(lease.borrower, lease.loanKeylet.key));
        env.close();
        BEAST_EXPECT(!env.le(lease.loanKeylet));
        BEAST_EXPECT(!env.le(keylet::account(lease.leaseAccount)));
        BEAST_EXPECT(env.ownerCount(lease.borrower) == ownersBefore - 2);
    }

    void
    testDefault(FeatureBitset features)
    {
        testcase("Default and recovery");
        using namespace jtx;
        using namespace loan;

        Env env{*this, features};
        Lease lease = setupBroker(env);
        createLease(env, lease, 4);
        Account const user1{"user1"};
        Account const user2{"user2"};
        Account const user3{"user3"};
        env.memoize(user1);
        env.memoize(user2);
        env.memoize(user3);
        createLeased(env, lease, user1, XRP(1));
        createLeased(env, lease, user2, XRP(1));
        createLeased(env, lease, user3, XRP(1));

        // Recovery before default: refused.
        env(manage(lease.lender, lease.loanKeylet.key, tfLoanLeaseRecover), Ter(tecNO_PERMISSION));

        auto const vaultPseudo = env.le(keylet::vault(lease.broker.vaultID))->at(sfAccount);
        auto const vaultBalanceBefore = accountBalance(env, vaultPseudo);

        passGracePeriod(env);
        env(manage(lease.lender, lease.loanKeylet.key, tfLoanDefault));
        env.close();

        // The free unit went to the vault at once; three units stay locked.
        auto const loanSle = env.le(lease.loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        BEAST_EXPECT(loanSle->isFlag(lsfLoanDefault));
        BEAST_EXPECTS(
            leaseBalance(env, lease) == XRPAmount{lease.reserve.drops() * 3},
            to_string(leaseBalance(env, lease)));
        BEAST_EXPECT(accountBalance(env, vaultPseudo) >= vaultBalanceBefore + lease.reserve);
        BEAST_EXPECT(loanSle->at(sfLeaseClaimVault) > beast::kZero);

        // Nothing free: recovery moves nothing.
        env(manage(lease.lender, lease.loanKeylet.key, tfLoanLeaseRecover), Ter(tecPRECISION_LOSS));

        // After default the borrower no longer acts for the lease account;
        // the broker owner does.
        env(pay(lease.stranger, user1, XRP(1'000)));
        env(pay(lease.stranger, user3, XRP(1'000)));
        env.close();
        env(sponsor::transfer(lease.borrower, tfSponsorshipEnd),
            sponsor::SponseeAcc(user1),
            Ter(tecNO_PERMISSION));
        env.close();
        env(sponsor::transfer(lease.lender, tfSponsorshipEnd), sponsor::SponseeAcc(user1));
        env.close();
        BEAST_EXPECT(leaseCount(env, lease) == 2);

        // Any account may run the recovery: the freed unit pays the vault.
        auto const claimBefore = env.le(lease.loanKeylet)->at(sfLeaseClaimVault);
        auto const vaultBalanceMid = accountBalance(env, vaultPseudo);
        env(manage(lease.stranger, lease.loanKeylet.key, tfLoanLeaseRecover));
        env.close();
        BEAST_EXPECT(leaseBalance(env, lease) == XRPAmount{lease.reserve.drops() * 2});
        BEAST_EXPECT(accountBalance(env, vaultPseudo) > vaultBalanceMid);
        BEAST_EXPECT(env.le(lease.loanKeylet)->at(sfLeaseClaimVault) < claimBefore);

        // A defaulted lease loan with a leased account left cannot be deleted.
        env(del(lease.lender, lease.loanKeylet.key), Ter(tecHAS_OBLIGATIONS));
        env.close();

        // AccountDelete after default pays the broker owner, not the borrower.
        incLgrSeqForAccDel(env, user3);
        auto const deleteFee = drops(env.current()->fees().increment);
        env(acctdelete(user3, lease.borrower), Fee(deleteFee), Ter(tecNO_SPONSOR_PERMISSION));
        env.close();
        env(acctdelete(user3, lease.lender), Fee(deleteFee));
        env.close();
        BEAST_EXPECT(!env.le(keylet::account(user3)));
        BEAST_EXPECT(leaseCount(env, lease) == 1);

        // The broker owner reaps the last, abandoned account (1 XRP, below its
        // own reserve); then the recovery and the delete complete.
        env(sponsor::transfer(lease.lender, tfSponsorshipReap),
            sponsor::SponseeAcc(user2),
            Fee(deleteFee));
        env.close();
        BEAST_EXPECT(leaseCount(env, lease) == 0);
        env(manage(lease.borrower, lease.loanKeylet.key, tfLoanLeaseRecover));
        env.close();
        BEAST_EXPECT(leaseBalance(env, lease) == XRPAmount{0});
        env(del(lease.borrower, lease.loanKeylet.key));
        env.close();
        BEAST_EXPECT(!env.le(lease.loanKeylet));
        BEAST_EXPECT(!env.le(keylet::account(lease.leaseAccount)));
    }
    void
    testSponsorshipObligation(FeatureBitset features)
    {
        using namespace jtx;
        for (bool const withLeasing : {false, true})
        {
            testcase << "AccountDelete of a Sponsorship owner "
                     << (withLeasing ? "with" : "without") << " AccountLeasing";
            Env env{*this, withLeasing ? features : features - featureAccountLeasing};
            Account const alice{"alice"};
            Account const bob{"bob"};
            Account const carol{"carol"};
            env.fund(XRP(10'000), alice, bob, carol);
            env.close();

            // alice prefunds fees for bob: alice owns a Sponsorship.
            env(sponsor::set_fee(alice, 0, XRP(10), XRP(1)), sponsor::SponseeAcc(bob));
            env.close();
            incLgrSeqForAccDel(env, alice);

            // Before AccountLeasing the Sponsorship is an obligation.
            env(acctdelete(alice, carol),
                Fee(drops(env.current()->fees().increment)),
                Ter(withLeasing ? TER{tesSUCCESS} : TER{tecHAS_OBLIGATIONS}));
            env.close();
            BEAST_EXPECT(static_cast<bool>(env.le(keylet::account(alice))) != withLeasing);
        }
    }

    void
    testReapFundedSponsorship(FeatureBitset features)
    {
        testcase("Reap refuses a sponsee that owns a funded Sponsorship");
        using namespace jtx;

        Env env{*this, features};
        Account const sponsorAcct{"sponsor"};
        Account const dave{"dave"};
        Account const erin{"erin"};
        env.fund(XRP(100'000), sponsorAcct, erin);
        env.memoize(dave);
        env.close();
        // Pass the first flag ledger, where the fee vote changes the reserves,
        // so the amounts below are sized with the reserves the reap sees.
        for (int i = 0; i < 260; ++i)
            env.close();

        auto const increment = env.current()->fees().increment;
        auto const feeAmount = XRP(1'000);
        // dave holds his owner reserve, the prefund and a little for fees,
        // which is below his own base reserve once the prefund is out.
        env(pay(sponsorAcct, dave, feeAmount + STAmount{increment} + drops(100)),
            Txflags(tfSponsorCreatedAccount),
            Fee(XRP(1)));
        env.close();
        env(sponsor::set_fee(dave, 0, feeAmount, XRP(1)), sponsor::SponseeAcc(erin));
        env.close();
        // Leave dave his owner reserve and 1 XRP: below his own base reserve.
        {
            auto const fee = env.current()->fees().base;
            // dave is account-sponsored and owns one object.
            auto const target = STAmount{env.current()->fees().accountReserve(1, 0)} + XRP(1);
            auto const balance = env.balance(dave);
            if (balance > target + STAmount{fee})
                env(pay(dave, erin, balance - target - STAmount{fee}));
            env.close();
        }
        incLgrSeqForAccDel(env, dave);

        // dave is under his own reserve, but the prefund is his: no reap.
        env(sponsor::transfer(sponsorAcct, tfSponsorshipReap),
            sponsor::SponseeAcc(dave),
            Fee(drops(increment)),
            Ter(tecHAS_OBLIGATIONS));
        env.close();
        BEAST_EXPECT(env.le(keylet::account(dave)));
    }

    void
    testDefaultAfterRepayment(FeatureBitset features)
    {
        testcase("Default after part of the principal is repaid");
        using namespace jtx;
        using namespace loan;

        Env env{*this, features};
        Lease lease = setupBroker(env);
        lease.principal = XRPAmount{lease.reserve.drops() * 3};
        lease.loanKeylet = nextLoanKeylet(env, lease.broker);
        env(set(lease.borrower,
                lease.broker.brokerID,
                Number{lease.principal.drops()},
                tfLoanLease),
            kCounterparty(lease.lender),
            Sig(sfCounterpartySignature, lease.lender),
            kPaymentTotal(3),
            Fee(env.current()->fees().base * 5));
        env.close();
        auto loanSle = env.le(lease.loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        lease.leaseAccount = *loanSle->at(~sfAccount);

        Account const user1{"user1"};
        env.memoize(user1);
        createLeased(env, lease, user1, XRP(1));

        // Two of three payments.
        for (int i = 0; i < 2; ++i)
        {
            loanSle = env.le(lease.loanKeylet);
            STAmount const payment{XRPAmount{static_cast<Number>(loanSle->at(sfPeriodicPayment))}};
            env(loan::pay(lease.borrower, lease.loanKeylet.key, payment), Fee(XRP(1)));
            env.close();
        }
        loanSle = env.le(lease.loanKeylet);
        if (!BEAST_EXPECT(loanSle && loanSle->at(sfPaymentRemaining) == 1))
            return;
        XRPAmount const outstanding{static_cast<Number>(loanSle->at(sfPrincipalOutstanding))};
        XRPAmount const freeBefore = leaseBalance(env, lease) - lease.reserve;

        auto const vaultPseudo = env.le(keylet::vault(lease.broker.vaultID))->at(sfAccount);
        auto const brokerPseudo = env.le(keylet::loanBroker(lease.broker.brokerID))->at(sfAccount);
        auto const vaultBefore = accountBalance(env, vaultPseudo);
        auto const brokerBefore = accountBalance(env, brokerPseudo);
        auto const vaultSleBefore = env.le(keylet::vault(lease.broker.vaultID));
        auto const brokerSleBefore = env.le(keylet::loanBroker(lease.broker.brokerID));
        Number const availableBefore = vaultSleBefore->at(sfAssetsAvailable);
        Number const totalBefore = vaultSleBefore->at(sfAssetsTotal);
        Number const coverBefore = brokerSleBefore->at(sfCoverAvailable);
        auto const borrowerBefore = accountBalance(env, lease.borrower);

        // Past the remaining payment's due date and its grace period.
        env.close(
            NetClock::time_point{NetClock::duration{
                loanSle->at(sfNextPaymentDueDate) + loanSle->at(sfGracePeriod) + 10}});
        env(manage(lease.lender, lease.loanKeylet.key, tfLoanDefault));
        env.close();

        // Both claims are paid at once; the vault gets the outstanding
        // principal, the cover gets back what it paid, the borrower the rest.
        loanSle = env.le(lease.loanKeylet);
        if (!BEAST_EXPECT(loanSle))
            return;
        BEAST_EXPECT(loanSle->at(sfLeaseClaimVault) == beast::kZero);
        BEAST_EXPECT(loanSle->at(sfLeaseClaimCover) == beast::kZero);
        BEAST_EXPECT(leaseBalance(env, lease) == lease.reserve);
        BEAST_EXPECTS(
            accountBalance(env, vaultPseudo) == vaultBefore + outstanding,
            to_string(accountBalance(env, vaultPseudo) - vaultBefore));
        BEAST_EXPECTS(
            accountBalance(env, brokerPseudo) == brokerBefore,
            to_string(accountBalance(env, brokerPseudo) - brokerBefore));
        // The vault's fields: the loss and its recovery cancel out, the cash
        // rises by the outstanding principal; the cover is whole again.
        auto const vaultSleAfter = env.le(keylet::vault(lease.broker.vaultID));
        auto const brokerSleAfter = env.le(keylet::loanBroker(lease.broker.brokerID));
        BEAST_EXPECTS(
            vaultSleAfter->at(sfAssetsAvailable) == availableBefore + Number{outstanding.drops()},
            to_string(vaultSleAfter->at(sfAssetsAvailable)));
        BEAST_EXPECTS(
            vaultSleAfter->at(sfAssetsTotal) == totalBefore,
            to_string(vaultSleAfter->at(sfAssetsTotal)));
        BEAST_EXPECTS(
            brokerSleAfter->at(sfCoverAvailable) == coverBefore,
            to_string(brokerSleAfter->at(sfCoverAvailable)));
        BEAST_EXPECTS(
            accountBalance(env, lease.borrower) == borrowerBefore + freeBefore - outstanding,
            to_string(accountBalance(env, lease.borrower) - borrowerBefore));
    }

    void
    testWithFeatures(FeatureBitset features)
    {
        testLeaseCreate(features);
        testLeaseCreateIOU(features);
        testLeasedAccount(features);
        testRelease(features);
        testPayoff(features);
        testDefault(features);
        testDefaultAfterRepayment(features);
        testSponsorshipObligation(features);
        testReapFundedSponsorship(features);
    }

public:
    void
    run() override
    {
        // Legacy vaults, then FixedPrecision vaults (LendingProtocolV1_1 and V1_2).
        testDisabled(all_);
        testWithFeatures(all_ - featureLendingProtocolV1_2);
        testWithFeatures(jtx::testableAmendments());
    }
};

BEAST_DEFINE_TESTSUITE(AccountLeasing, app, xrpl);

}  // namespace xrpl::test
